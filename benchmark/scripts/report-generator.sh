#!/bin/bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <data-dir>" >&2
    echo "  data-dir: e.g., benchmark/data/aarch64/2026-05-06/" >&2
    exit 1
fi

DATA_DIR="$1"
BENCHMARK_DIR=$(cd "$(dirname "$0")/.." && pwd)
REPORT_FILE="$BENCHMARK_DIR/REPORT.md"

# Extract arch and date from path
ARCH=$(basename "$(dirname "$DATA_DIR")")
DATE=$(basename "$DATA_DIR")

# Percentile calculator: reads sorted values from stdin, prints value at given percentile
# Usage: echo "values" | percentile <p>
percentile() {
    awk -v p="$1" '
    { vals[NR] = $1 }
    END {
        if (NR == 0) { print 0; exit }
        rank = (p / 100.0) * (NR - 1) + 1
        if (rank <= 1) { print vals[1]; exit }
        if (rank >= NR) { print vals[NR]; exit }
        lo = int(rank)
        hi = lo + 1
        frac = rank - lo
        printf "%.1f\n", vals[lo] + frac * (vals[hi] - vals[lo])
    }'
}

# Compute memory stats for a scenario directory
# Outputs: median_pss p95_pss p99_pss max_pss median_rss
compute_memory_stats() {
    local scenario_dir="$1"
    local csv="$scenario_dir/memory.csv"
    if [[ ! -f "$csv" ]]; then
        echo "0 0 0 0 0"
        return
    fi
    # Sum PSS per timestamp (all daemons combined)
    local pss_values
    pss_values=$(awk -F, 'NR>1 { sums[$1] += $4 } END { for (t in sums) print sums[t] }' "$csv" | sort -n)

    local rss_values
    rss_values=$(awk -F, 'NR>1 { sums[$1] += $5 } END { for (t in sums) print sums[t] }' "$csv" | sort -n)

    local median_pss p95_pss p99_pss max_pss median_rss
    median_pss=$(echo "$pss_values" | percentile 50)
    p95_pss=$(echo "$pss_values" | percentile 95)
    p99_pss=$(echo "$pss_values" | percentile 99)
    max_pss=$(echo "$pss_values" | sort -n | tail -1)
    median_rss=$(echo "$rss_values" | percentile 50)

    echo "$median_pss $p95_pss $p99_pss $max_pss $median_rss"
}

# Compute CPU stats for a scenario directory
# Outputs: avg_usr avg_idle
compute_cpu_stats() {
    local scenario_dir="$1"
    local csv="$scenario_dir/cpu.csv"
    if [[ ! -f "$csv" ]]; then
        echo "0 0"
        return
    fi
    awk -F, 'NR>1 { usr += $2; idle += $4; n++ } END {
        if (n > 0) printf "%.1f %.1f\n", usr/n, idle/n
        else print "0 0"
    }' "$csv"
}

# Per-daemon breakdown for a scenario
# Outputs lines: daemon median_pss median_rss
compute_per_daemon() {
    local scenario_dir="$1"
    local csv="$scenario_dir/memory.csv"
    if [[ ! -f "$csv" ]]; then
        return
    fi
    # Get unique daemons
    local daemons
    daemons=$(awk -F, 'NR>1 { print $2 }' "$csv" | sort -u)
    for daemon in $daemons; do
        local pss_med rss_med
        pss_med=$(awk -F, -v d="$daemon" 'NR>1 && $2==d { print $4 }' "$csv" | sort -n | percentile 50)
        rss_med=$(awk -F, -v d="$daemon" 'NR>1 && $2==d { print $5 }' "$csv" | sort -n | percentile 50)
        echo "$daemon $pss_med $rss_med"
    done
}

# Build report
report=""
report+="## $ARCH — $DATE"$'\n\n'

# Summary table
report+="### Summary"$'\n\n'
report+="| Scenario | Median PSS (KB) | P95 PSS | P99 PSS | Max PSS | Median RSS (KB) | CPU usr% | CPU idle% |"$'\n'
report+="|----------|-----------------|---------|---------|---------|-----------------|----------|-----------|"$'\n'

for scenario in baseline simple-component realistic-load; do
    scenario_dir="$DATA_DIR/$scenario"
    if [[ ! -d "$scenario_dir" ]]; then
        continue
    fi
    read -r med_pss p95_pss p99_pss max_pss med_rss <<< "$(compute_memory_stats "$scenario_dir")"
    read -r cpu_usr cpu_idle <<< "$(compute_cpu_stats "$scenario_dir")"
    report+="| $scenario | $med_pss | $p95_pss | $p99_pss | $max_pss | $med_rss | $cpu_usr | $cpu_idle |"$'\n'
done
report+=$'\n'

# Per-daemon breakdown at realistic-load
report+="### Per-Daemon Breakdown (realistic-load)"$'\n\n'
report+="| Daemon | Median PSS (KB) | Median RSS (KB) |"$'\n'
report+="|--------|-----------------|-----------------|"$'\n'

if [[ -d "$DATA_DIR/realistic-load" ]]; then
    while read -r daemon pss rss; do
        report+="| $daemon | $pss | $rss |"$'\n'
    done <<< "$(compute_per_daemon "$DATA_DIR/realistic-load")"
fi
report+=$'\n'

# Startup time
# run-all.sh measures startup-to-active once (writes startup-timing.txt at DATA_DIR level).
# measure.sh captures critical-chain per-scenario (startup-critical-chain.txt).
report+="### Startup Time"$'\n\n'
if [[ -f "$DATA_DIR/startup-timing.txt" ]]; then
    report+='```'$'\n'
    report+="$(cat "$DATA_DIR/startup-timing.txt")"$'\n'
    report+='```'$'\n'
elif [[ -f "$DATA_DIR/baseline/startup-critical-chain.txt" ]]; then
    report+='```'$'\n'
    report+="$(cat "$DATA_DIR/baseline/startup-critical-chain.txt")"$'\n'
    report+='```'$'\n'
else
    report+="No startup data available."$'\n'
fi
report+=$'\n'

# Disk usage
report+="### Disk Usage"$'\n\n'
if [[ -f "$DATA_DIR/realistic-load/disk.txt" ]]; then
    report+='```'$'\n'
    report+="$(cat "$DATA_DIR/realistic-load/disk.txt")"$'\n'
    report+='```'$'\n'
elif [[ -f "$DATA_DIR/baseline/disk.txt" ]]; then
    report+='```'$'\n'
    report+="$(cat "$DATA_DIR/baseline/disk.txt")"$'\n'
    report+='```'$'\n'
else
    report+="No disk data available."$'\n'
fi
report+=$'\n---\n\n'

# Output to stdout
echo "$report"

# Append to REPORT.md
if [[ ! -f "$REPORT_FILE" ]]; then
    cat > "$REPORT_FILE" << 'EOF'
# GGLite Resource Benchmark Report

Detailed benchmark report with full methodology and raw data.
See `benchmark/README.md` for context and `docs/RESOURCE_LIMITS.md` for the customer-facing summary.

---

EOF
fi

echo "$report" >> "$REPORT_FILE"
echo "Report appended to $REPORT_FILE"
