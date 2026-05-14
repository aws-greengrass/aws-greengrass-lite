#!/bin/bash
set -euo pipefail

# measure.sh — Concurrent resource measurement for GGLite daemons
# Usage: measure.sh <output-dir> [--duration <minutes>] [--interval <seconds>] [--warmup <minutes>]

# --- Argument parsing ---
OUTPUT_DIR="${1:-}"
shift || true
DURATION=10
INTERVAL=10
WARMUP=5

while [[ $# -gt 0 ]]; do
    case "$1" in
        --duration) DURATION="$2"; shift 2 ;;
        --interval) INTERVAL="$2"; shift 2 ;;
        --warmup)   WARMUP="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [[ -z "$OUTPUT_DIR" ]]; then
    echo "Usage: measure.sh <output-dir> [--duration <minutes>] [--interval <seconds>] [--warmup <minutes>]" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

# --- Tool assertions ---
for tool in smem mpstat systemd-analyze; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: Required tool '$tool' not found. Install it before running." >&2
        exit 1
    fi
done

# Check smaps_rollup accessibility (kernel 4.14+)
if ! ls /proc/1/smaps_rollup &>/dev/null 2>&1; then
    echo "ERROR: /proc/<pid>/smaps_rollup not accessible. Requires kernel 4.14+." >&2
    exit 1
fi

# --- Constants ---
# Target daemon set. /proc/<pid>/comm is truncated to 15 chars
# (TASK_COMM_LEN), which hides gg-fleet-statusd (16 chars). Read the full
# executable name from /proc/<pid>/cmdline basename instead.
DAEMON_NAMES=(
    ggconfigd
    ggdeploymentd
    gghealthd
    ggipcd
    ggpubsubd
    iotcored
    tesd
    tes-serverd
    gg-fleet-statusd
    recipe-runner
)
# Also match user components deployed by the scenarios. systemd service name
# is ggl.<component-name>.service; the process argv[0] uses the component name
# directly (e.g., "com.example.HelloWorld").
USER_COMPONENT_PREFIX='com.example.'
COUNT=$(( (DURATION * 60) / INTERVAL ))

# --- Helper: discover GGLite daemon PIDs ---
# Reads /proc/<pid>/cmdline basename to get the FULL executable name,
# which avoids the 15-char TASK_COMM_LEN truncation affecting /proc/<pid>/comm.
# Returns lines: "<pid> <name>"
discover_pids() {
    local pid cmdline name
    for pid in /proc/[0-9]*; do
        pid="${pid##*/}"
        [[ -r "/proc/${pid}/cmdline" ]] || continue
        # cmdline is NUL-separated; argv[0] is everything up to the first NUL.
        # Empty cmdline means kernel thread — skip.
        cmdline=$(tr '\0' '\n' < "/proc/${pid}/cmdline" 2>/dev/null | head -1)
        [[ -n "$cmdline" ]] || continue
        name="${cmdline##*/}"
        # Match against known daemon set
        for d in "${DAEMON_NAMES[@]}"; do
            if [[ "$name" == "$d" ]]; then
                echo "$pid $name"
                continue 2
            fi
        done
        # Match user components (scenario-deployed components)
        if [[ "$name" == "${USER_COMPONENT_PREFIX}"* ]]; then
            echo "$pid $name"
        fi
    done
}

# --- Warmup ---
echo "Warmup: waiting ${WARMUP} minutes before measurement..."
warmup_secs=$((WARMUP * 60))
elapsed=0
while [[ $elapsed -lt $warmup_secs ]]; do
    remaining=$(( (warmup_secs - elapsed) / 60 ))
    echo "  Warmup: ${remaining}m remaining..."
    sleep_time=30
    if [[ $((warmup_secs - elapsed)) -lt 30 ]]; then
        sleep_time=$((warmup_secs - elapsed))
    fi
    sleep "$sleep_time"
    elapsed=$((elapsed + sleep_time))
done
echo "Warmup complete. Starting measurements."

# --- Measurement 1: Memory via smem ---
# Note: smem -P matches on command line (not truncated), but by-PID filtering is
# more reliable when multiple processes share a name prefix. We use smem's -k
# option plus awk filter on the discovered PID.
measure_smem() {
    local outfile="$OUTPUT_DIR/memory.csv"
    echo "timestamp,daemon,pid,pss_kb,rss_kb,uss_kb,vss_kb" > "$outfile"
    local i=0
    while [[ $i -lt $COUNT ]]; do
        local ts
        ts=$(date -u +%Y-%m-%dT%H:%M:%SZ)
        # Build a pipe-separated list of PIDs we want, then run smem once
        local pid_list=""
        local -A pid_to_name=()
        while IFS=' ' read -r pid name; do
            pid_list="${pid_list}|^${pid}$"
            pid_to_name["$pid"]="$name"
        done < <(discover_pids)
        pid_list="${pid_list#|}"
        [[ -n "$pid_list" ]] || { sleep "$INTERVAL"; i=$((i + 1)); continue; }

        # smem -c columns; filter by PID in awk using our known pid set.
        while IFS= read -r line; do
            # line: pid user command pss rss uss vss ... (varies); we used -c
            local pid pss rss uss vss
            pid=$(awk '{print $1}' <<<"$line")
            pss=$(awk '{print $2}' <<<"$line")
            rss=$(awk '{print $3}' <<<"$line")
            uss=$(awk '{print $4}' <<<"$line")
            vss=$(awk '{print $5}' <<<"$line")
            local name="${pid_to_name[$pid]:-}"
            [[ -n "$name" ]] || continue
            echo "${ts},${name},${pid},${pss},${rss},${uss},${vss}" >> "$outfile"
        done < <(smem -c "pid pss rss uss vss" -H 2>/dev/null | awk -v pids="${pid_list}" '
            BEGIN {
                # Build an exact-match table for fast lookup
                n = split(pids, arr, "|")
                for (j = 1; j <= n; j++) {
                    # Strip the ^$ anchors
                    p = arr[j]
                    sub(/^\^/, "", p); sub(/\$$/, "", p)
                    match_pids[p] = 1
                }
            }
            $1 in match_pids { print }
        ')
        sleep "$INTERVAL"
        i=$((i + 1))
    done
}

# --- Measurement 2: Memory via smaps_rollup ---
measure_smaps() {
    local outfile="$OUTPUT_DIR/smaps.csv"
    echo "timestamp,daemon,pid,pss_kb,rss_kb,shared_clean_kb,shared_dirty_kb,private_clean_kb,private_dirty_kb" > "$outfile"
    local i=0
    while [[ $i -lt $COUNT ]]; do
        local ts
        ts=$(date -u +%Y-%m-%dT%H:%M:%SZ)
        while IFS=' ' read -r pid comm; do
            local rollup="/proc/${pid}/smaps_rollup"
            if [[ -r "$rollup" ]]; then
                local pss rss sc sd pc pd
                pss=$(awk '/^Pss:/{print $2}' "$rollup" 2>/dev/null || echo 0)
                rss=$(awk '/^Rss:/{print $2}' "$rollup" 2>/dev/null || echo 0)
                sc=$(awk '/^Shared_Clean:/{print $2}' "$rollup" 2>/dev/null || echo 0)
                sd=$(awk '/^Shared_Dirty:/{print $2}' "$rollup" 2>/dev/null || echo 0)
                pc=$(awk '/^Private_Clean:/{print $2}' "$rollup" 2>/dev/null || echo 0)
                pd=$(awk '/^Private_Dirty:/{print $2}' "$rollup" 2>/dev/null || echo 0)
                echo "${ts},${comm},${pid},${pss},${rss},${sc},${sd},${pc},${pd}" >> "$outfile"
            fi
        done < <(discover_pids)
        sleep "$INTERVAL"
        i=$((i + 1))
    done
}

# --- Measurement 3: CPU via mpstat ---
measure_cpu() {
    local outfile="$OUTPUT_DIR/cpu.csv"
    echo "timestamp,usr,sys,idle,iowait" > "$outfile"
    mpstat "$INTERVAL" "$COUNT" | awk '
        /^[0-9]/ && NF >= 12 && !/CPU/ {
            cmd = "date -u +%Y-%m-%dT%H:%M:%SZ"
            cmd | getline ts
            close(cmd)
            print ts","$3","$5","$12","$6
        }
    ' >> "$outfile"
}

# --- Measurement 4: Passive startup info capture ---
# Captures systemd-analyze critical-chain for per-service breakdown.
# The actual start-to-active duration is measured ONCE by run-all.sh
# (before any scenarios run) since restarting the target here would kill
# any components currently being measured.
measure_startup() {
    local outfile="$OUTPUT_DIR/startup-critical-chain.txt"
    systemd-analyze critical-chain greengrass-lite.target > "$outfile" 2>&1 || true
}

# --- Launch concurrent measurements ---
echo "Starting measurements: duration=${DURATION}m, interval=${INTERVAL}s, samples=${COUNT}"

measure_smem &
PID_SMEM=$!
measure_smaps &
PID_SMAPS=$!
measure_cpu &
PID_CPU=$!
measure_startup &
PID_STARTUP=$!

wait $PID_SMEM $PID_SMAPS $PID_CPU $PID_STARTUP

# --- Disk usage ---
# dpkg -L returns both directories AND files; piping it directly to `du -cb`
# sums the entire system /lib/systemd/system directory (4.5 GB of every
# package's unit files). We filter to regular files only, and cross-check
# against dpkg-query's advertised Installed-Size.
write_disk_report() {
    local advertised_kb actual_bytes actual_kb
    echo "=== Install size ==="
    # Advertised installed size from package metadata (KB)
    advertised_kb=$(dpkg-query -W -f='${Installed-Size}' aws-greengrass-lite 2>/dev/null || echo "N/A")
    echo "  Advertised (dpkg Installed-Size): ${advertised_kb} KB"
    # Sum of actual installed file sizes (filter to regular files, not dirs)
    actual_bytes=$(dpkg -L aws-greengrass-lite 2>/dev/null \
        | while IFS= read -r f; do [[ -f "$f" ]] && stat -c '%s' "$f" 2>/dev/null; done \
        | awk '{s+=$1} END {print s+0}')
    if [[ -n "$actual_bytes" && "$actual_bytes" != "0" ]]; then
        actual_kb=$((actual_bytes / 1024))
        echo "  Actual installed files:           ${actual_bytes} bytes (${actual_kb} KB)"
    else
        echo "  Actual installed files:           N/A (package not installed via dpkg)"
    fi
    echo ""
    echo "=== Runtime size ==="
    du -sh /var/lib/greengrass 2>/dev/null || echo "N/A (/var/lib/greengrass not found)"
}
write_disk_report > "$OUTPUT_DIR/disk.txt"

# --- Summary ---
smem_samples=$(( $(wc -l < "$OUTPUT_DIR/memory.csv") - 1 ))
smaps_samples=$(( $(wc -l < "$OUTPUT_DIR/smaps.csv") - 1 ))
cpu_samples=$(( $(wc -l < "$OUTPUT_DIR/cpu.csv") - 1 ))

echo ""
echo "=== Measurement Complete ==="
echo "  Duration:       ${DURATION} minutes"
echo "  Interval:       ${INTERVAL} seconds"
echo "  Memory (smem):  ${smem_samples} samples"
echo "  Memory (smaps): ${smaps_samples} samples"
echo "  CPU (mpstat):   ${cpu_samples} samples"
echo "  Startup:        captured"
echo "  Disk:           captured"
echo "  Output:         ${OUTPUT_DIR}"
