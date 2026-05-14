#!/bin/bash
set -euo pipefail

# measure-deployment.sh — High-frequency (1-sec) resource sampler for deployment windows
#
# Usage: measure-deployment.sh <output-dir> [--duration <seconds>]
#   Default duration: 300 seconds (5 minutes)
#
# Runs four concurrent background samplers at 1-second granularity:
#   1. PSS/RSS via /proc/<pid>/smaps_rollup
#   2. CPU via /proc/stat delta computation
#   3. Network via /proc/net/dev delta computation
#   4. Disk I/O via /proc/diskstats delta computation
#
# All samplers use direct /proc polling — no external dependencies beyond bash + awk.
# This avoids buffering issues with sysstat tools and the iostat since-boot
# average artifact.
#
# Outputs in <output-dir>/:
#   deployment-timeseries.csv  — per-daemon PSS/RSS per second
#   cpu-deployment.csv         — system CPU per second
#   network.csv                — per-interface network throughput per second
#   diskio.csv                 — per-device disk I/O per second
#   deployment-peaks.csv       — computed peak/total metrics (written after samplers finish)
#
# Signal handling: SIGTERM/SIGINT → flush partial CSVs + compute peaks from collected data.

# --- Argument parsing ---
OUTPUT_DIR="${1:-}"
shift || true
DURATION=300

while [[ $# -gt 0 ]]; do
    case "$1" in
        --duration) DURATION="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [[ -z "$OUTPUT_DIR" ]]; then
    echo "Usage: measure-deployment.sh <output-dir> [--duration <seconds>]" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

# --- Tool availability checks ---
# Only /proc filesystem access is required — no sysstat tools needed.

# Verify smaps_rollup access
if ! ls /proc/1/smaps_rollup &>/dev/null 2>&1; then
    echo "ERROR: /proc/<pid>/smaps_rollup not accessible. Requires kernel 4.14+." >&2
    exit 1
fi

# Verify /proc/stat readable (CPU sampler)
if [[ ! -r /proc/stat ]]; then
    echo "ERROR: /proc/stat not readable." >&2
    exit 1
fi

# Verify /proc/net/dev readable (network sampler)
if [[ ! -r /proc/net/dev ]]; then
    echo "ERROR: /proc/net/dev not readable." >&2
    exit 1
fi

# Verify /proc/diskstats readable (disk I/O sampler)
if [[ ! -r /proc/diskstats ]]; then
    echo "ERROR: /proc/diskstats not readable." >&2
    exit 1
fi

# --- Constants: daemon discovery (same as measure.sh) ---
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
USER_COMPONENT_PREFIX='com.example.'

discover_pids() {
    local pid cmdline name
    for pid in /proc/[0-9]*; do
        pid="${pid##*/}"
        [[ -r "/proc/${pid}/cmdline" ]] || continue
        cmdline=$(tr '\0' '\n' < "/proc/${pid}/cmdline" 2>/dev/null | head -1)
        [[ -n "$cmdline" ]] || continue
        name="${cmdline##*/}"
        for d in "${DAEMON_NAMES[@]}"; do
            if [[ "$name" == "$d" ]]; then
                echo "$pid $name"
                continue 2
            fi
        done
        if [[ "$name" == "${USER_COMPONENT_PREFIX}"* ]]; then
            echo "$pid $name"
        fi
    done
}

# --- Background job PIDs for cleanup ---
SAMPLER_PIDS=()

cleanup() {
    # Kill direct children
    for pid in "${SAMPLER_PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    # Kill grandchildren
    pkill -P $$ 2>/dev/null || true
    wait 2>/dev/null || true
    compute_peaks
}

trap cleanup SIGTERM SIGINT EXIT

# --- Sampler 1: PSS/RSS via smaps_rollup ---
sample_memory() {
    local outfile="$OUTPUT_DIR/deployment-timeseries.csv"
    echo "timestamp,daemon,pid,pss_kb,rss_kb" > "$outfile"
    local end_time=$(( $(date +%s) + DURATION ))
    while [[ $(date +%s) -lt $end_time ]]; do
        local ts
        ts=$(date -u +%Y-%m-%dT%H:%M:%SZ)
        while IFS=' ' read -r pid name; do
            local rollup="/proc/${pid}/smaps_rollup"
            if [[ -r "$rollup" ]]; then
                local pss rss
                pss=$(awk '/^Pss:/{print $2}' "$rollup" 2>/dev/null || echo 0)
                rss=$(awk '/^Rss:/{print $2}' "$rollup" 2>/dev/null || echo 0)
                echo "${ts},${name},${pid},${pss},${rss}" >> "$outfile"
            fi
        done < <(discover_pids)
        sleep 1
    done
}

# --- Sampler 2: CPU via /proc/stat ---
# Reads the aggregate "cpu " line from /proc/stat every 1 second.
# Computes delta of user, nice, system, idle, iowait, irq, softirq, steal
# counters between consecutive samples, then derives percentages.
sample_cpu() {
    local outfile="$OUTPUT_DIR/cpu-deployment.csv"
    echo "timestamp,usr,sys,idle,iowait" > "$outfile"
    local end_time=$(( $(date +%s) + DURATION ))

    # Read initial counters
    local prev_user prev_nice prev_system prev_idle prev_iowait prev_irq prev_softirq prev_steal
    read -r _ prev_user prev_nice prev_system prev_idle prev_iowait prev_irq prev_softirq prev_steal _ < <(grep '^cpu ' /proc/stat)

    sleep 1

    while [[ $(date +%s) -lt $end_time ]]; do
        local ts cur_user cur_nice cur_system cur_idle cur_iowait cur_irq cur_softirq cur_steal
        ts=$(date -u +%Y-%m-%dT%H:%M:%SZ)
        read -r _ cur_user cur_nice cur_system cur_idle cur_iowait cur_irq cur_softirq cur_steal _ < <(grep '^cpu ' /proc/stat)

        # Compute deltas
        local d_user=$(( cur_user - prev_user + cur_nice - prev_nice ))
        local d_system=$(( cur_system - prev_system + cur_irq - prev_irq + cur_softirq - prev_softirq + cur_steal - prev_steal ))
        local d_idle=$(( cur_idle - prev_idle ))
        local d_iowait=$(( cur_iowait - prev_iowait ))
        local d_total=$(( d_user + d_system + d_idle + d_iowait ))

        if [[ $d_total -gt 0 ]]; then
            awk -v u="$d_user" -v s="$d_system" -v i="$d_idle" -v w="$d_iowait" -v t="$d_total" \
                'BEGIN { printf "%.1f,%.1f,%.1f,%.1f\n", u*100/t, s*100/t, i*100/t, w*100/t }' \
                | while IFS= read -r line; do echo "${ts},${line}" >> "$outfile"; done
        fi

        prev_user=$cur_user; prev_nice=$cur_nice; prev_system=$cur_system
        prev_idle=$cur_idle; prev_iowait=$cur_iowait; prev_irq=$cur_irq
        prev_softirq=$cur_softirq; prev_steal=$cur_steal

        sleep 1
    done
}

# --- Sampler 3: Network via /proc/net/dev ---
# Reads /proc/net/dev every 1 second. Computes per-interface byte/packet deltas.
# Skips the loopback (lo) interface. Reports kB/s and pkt/s.
sample_network() {
    local outfile="$OUTPUT_DIR/network.csv"
    echo "timestamp,iface,rx_kB_per_s,tx_kB_per_s,rx_pkt_per_s,tx_pkt_per_s" > "$outfile"
    local end_time=$(( $(date +%s) + DURATION ))
    declare -A prev_rx prev_tx prev_rx_pkt prev_tx_pkt
    local first=true
    while [[ $(date +%s) -lt $end_time ]]; do
        local ts
        ts=$(date -u +%Y-%m-%dT%H:%M:%SZ)
        while IFS=': ' read -r iface rx_bytes rx_pkt _ _ _ _ _ _ tx_bytes tx_pkt _; do
            [[ "$iface" == "lo" ]] && continue
            [[ -z "$iface" ]] && continue
            if [[ "$first" != "true" ]]; then
                local drx=$(( (rx_bytes - ${prev_rx[$iface]:-0}) / 1024 ))
                local dtx=$(( (tx_bytes - ${prev_tx[$iface]:-0}) / 1024 ))
                local drx_pkt=$(( rx_pkt - ${prev_rx_pkt[$iface]:-0} ))
                local dtx_pkt=$(( tx_pkt - ${prev_tx_pkt[$iface]:-0} ))
                echo "${ts},${iface},${drx},${dtx},${drx_pkt},${dtx_pkt}" >> "$outfile"
            fi
            prev_rx[$iface]=$rx_bytes
            prev_tx[$iface]=$tx_bytes
            prev_rx_pkt[$iface]=$rx_pkt
            prev_tx_pkt[$iface]=$tx_pkt
        done < <(tail -n +3 /proc/net/dev | sed 's/^[[:space:]]*//')
        first=false
        sleep 1
    done
}

# --- Sampler 4: Disk I/O via /proc/diskstats ---
# Reads /proc/diskstats every 1 second. Filters whole-device entries only
# (sd*, nvme*n*, mmcblk* without partition suffix). Computes sector deltas
# and converts to kB (512-byte sectors → /2 = kB).
# Note: util_pct is always 0 — /proc/diskstats does not expose io_ticks
# in a way that reliably maps to utilization percentage. This is a known
# limitation; the column is preserved for CSV schema compatibility.
sample_diskio() {
    local outfile="$OUTPUT_DIR/diskio.csv"
    echo "timestamp,device,rkB_per_s,wkB_per_s,r_iops,w_iops,util_pct" > "$outfile"
    local end_time=$(( $(date +%s) + DURATION ))
    declare -A prev_rsect prev_wsect prev_rio prev_wio
    local first=true
    while [[ $(date +%s) -lt $end_time ]]; do
        local ts
        ts=$(date -u +%Y-%m-%dT%H:%M:%SZ)
        while read -r _ _ dev rio _ rsect _ wio _ wsect _; do
            # Skip partitions — only whole devices
            [[ "$dev" =~ ^(sd[a-z]+|nvme[0-9]+n[0-9]+|mmcblk[0-9]+)$ ]] || continue
            if [[ "$first" != "true" ]]; then
                local dr=$(( (rsect - ${prev_rsect[$dev]:-0}) / 2 ))  # sectors→kB (512B sectors)
                local dw=$(( (wsect - ${prev_wsect[$dev]:-0}) / 2 ))
                local drio=$(( rio - ${prev_rio[$dev]:-0} ))
                local dwio=$(( wio - ${prev_wio[$dev]:-0} ))
                echo "${ts},${dev},${dr},${dw},${drio},${dwio},0" >> "$outfile"
            fi
            prev_rsect[$dev]=$rsect
            prev_wsect[$dev]=$wsect
            prev_rio[$dev]=$rio
            prev_wio[$dev]=$wio
        done < /proc/diskstats
        first=false
        sleep 1
    done
}

# --- Compute deployment-peaks.csv from collected data ---
compute_peaks() {
    local outfile="$OUTPUT_DIR/deployment-peaks.csv"
    echo "metric,value,unit" > "$outfile"

    local timeseries="$OUTPUT_DIR/deployment-timeseries.csv"
    local cpufile="$OUTPUT_DIR/cpu-deployment.csv"
    local netfile="$OUTPUT_DIR/network.csv"
    local diskfile="$OUTPUT_DIR/diskio.csv"

    # Peak total PSS (sum all daemons per timestamp, take max)
    if [[ -f "$timeseries" ]] && [[ $(wc -l < "$timeseries") -gt 1 ]]; then
        awk -F, 'NR>1 {sum[$1]+=$4} END {max=0; for(t in sum) if(sum[t]>max) max=sum[t]; print "peak_total_pss_kb,"max",kB"}' "$timeseries" >> "$outfile"
    else
        echo "WARN: deployment-timeseries.csv has no samples — peak PSS computation skipped" >&2
        echo "peak_total_pss_kb,NaN,kB" >> "$outfile"
    fi

    # CPU peaks
    if [[ -f "$cpufile" ]] && [[ $(wc -l < "$cpufile") -gt 1 ]]; then
        awk -F, 'NR>1 {if($2>mu) mu=$2; if($3>ms) ms=$3; if((100-$4)>mb) mb=100-$4} END {
            printf "peak_cpu_user_pct,%.1f,%%\n",mu;
            printf "peak_cpu_system_pct,%.1f,%%\n",ms;
            printf "peak_cpu_busy_pct,%.1f,%%\n",mb
        }' "$cpufile" >> "$outfile"
    else
        echo "WARN: cpu-deployment.csv has no samples — peak CPU computation skipped" >&2
        {
            echo "peak_cpu_user_pct,NaN,%"
            echo "peak_cpu_system_pct,NaN,%"
            echo "peak_cpu_busy_pct,NaN,%"
        } >> "$outfile"
    fi

    # Network peaks and totals
    if [[ -f "$netfile" ]] && [[ $(wc -l < "$netfile") -gt 1 ]]; then
        awk -F, 'NR>1 {
            if($3>prx) prx=$3; if($4>ptx) ptx=$4;
            trx+=$3; ttx+=$4
        } END {
            printf "peak_net_rx_kB_s,%.1f,kB/s\n",prx;
            printf "peak_net_tx_kB_s,%.1f,kB/s\n",ptx;
            printf "total_net_rx_kB,%.1f,kB\n",trx;
            printf "total_net_tx_kB,%.1f,kB\n",ttx
        }' "$netfile" >> "$outfile"
    else
        echo "WARN: network.csv has no samples — peak network computation skipped" >&2
        {
            echo "peak_net_rx_kB_s,NaN,kB/s"
            echo "peak_net_tx_kB_s,NaN,kB/s"
            echo "total_net_rx_kB,NaN,kB"
            echo "total_net_tx_kB,NaN,kB"
        } >> "$outfile"
    fi

    # Disk I/O peaks and totals
    if [[ -f "$diskfile" ]] && [[ $(wc -l < "$diskfile") -gt 1 ]]; then
        awk -F, 'NR>1 {
            if($4>pw) pw=$4; if($3>pr) pr=$3;
            tw+=$4; tr+=$3;
            if($7>pu) pu=$7
        } END {
            printf "peak_disk_write_kB_s,%.1f,kB/s\n",pw;
            printf "peak_disk_read_kB_s,%.1f,kB/s\n",pr;
            printf "total_disk_write_kB,%.1f,kB\n",tw;
            printf "total_disk_read_kB,%.1f,kB\n",tr;
            printf "peak_disk_util_pct,%.1f,%%\n",pu
        }' "$diskfile" >> "$outfile"
    else
        echo "WARN: diskio.csv has no samples — peak disk I/O computation skipped" >&2
        {
            echo "peak_disk_write_kB_s,NaN,kB/s"
            echo "peak_disk_read_kB_s,NaN,kB/s"
            echo "total_disk_write_kB,NaN,kB"
            echo "total_disk_read_kB,NaN,kB"
            echo "peak_disk_util_pct,NaN,%"
        } >> "$outfile"
    fi

    echo "sample_duration_sec,${DURATION},s" >> "$outfile"
    echo "Peaks written to: $outfile"
}

# --- Launch concurrent samplers ---
echo "measure-deployment: starting ${DURATION}s sampling at 1-sec granularity"
echo "  Output: $OUTPUT_DIR"

sample_memory &
SAMPLER_PIDS+=($!)

sample_cpu &
SAMPLER_PIDS+=($!)

sample_network &
SAMPLER_PIDS+=($!)

sample_diskio &
SAMPLER_PIDS+=($!)

# Wait for all samplers to complete naturally
wait "${SAMPLER_PIDS[@]}" 2>/dev/null || true
SAMPLER_PIDS=()

# Compute peaks (trap also calls this, but only if we didn't finish naturally)
compute_peaks

echo "measure-deployment: sampling complete"
