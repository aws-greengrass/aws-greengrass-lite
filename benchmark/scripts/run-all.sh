#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BENCHMARK_DIR=$(cd "$SCRIPT_DIR/.." && pwd)

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <arch> [--phase2]" >&2
    echo "  arch: aarch64, x86_64, armv7l" >&2
    echo "  --phase2: also run cloud deployment scenarios (Phase 2)" >&2
    exit 1
fi

ARCH="$1"
shift
RUN_PHASE2=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --phase2) RUN_PHASE2=true; shift ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done
DATE=$(date +%Y-%m-%d)
OUTPUT_DIR="$BENCHMARK_DIR/data/$ARCH/$DATE"
mkdir -p "$OUTPUT_DIR"

# Tee all output to log while printing to terminal
exec > >(tee -a "$OUTPUT_DIR/run-all.log") 2>&1

START_TIME=$(date +%s)

echo "============================================"
echo " GGLite Benchmark — $ARCH"
echo " Date: $DATE"
echo " Start: $(date --iso-8601=seconds)"
echo " Output: $OUTPUT_DIR"
echo "============================================"
echo

# Step 1: Measure startup time (restart target, time until all services active)
# Must run BEFORE scenarios since it restarts the target and resets state.
echo ">>> Measuring startup time (restart-to-active)..."
STARTUP_LOG="$OUTPUT_DIR/startup-timing.txt"
{
    echo "=== Startup time measurement (restart greengrass-lite.target) ==="
    # Get services managed by the target
    SERVICES=$(systemctl list-dependencies --plain --no-legend greengrass-lite.target 2>/dev/null \
        | awk '/\.service$/ {print $1}')
    if [[ -z "$SERVICES" ]]; then
        echo "  ERROR: No services found under greengrass-lite.target"
    else
        SVC_COUNT=$(echo "$SERVICES" | wc -l)
        echo "  Services under target: ${SVC_COUNT}"
        # Clear any failed state first
        systemctl reset-failed 2>/dev/null || true
        # Restart target
        RESTART_NS=$(date +%s%N)
        systemctl restart greengrass-lite.target 2>&1 | sed 's/^/  /' || true
        # Poll until all services are active (or timeout after 120s)
        DEADLINE=$((SECONDS + 120))
        ALL_ACTIVE=false
        while (( SECONDS < DEADLINE )); do
            PENDING=0
            for svc in $SERVICES; do
                if ! systemctl is-active --quiet "$svc" 2>/dev/null; then
                    PENDING=$((PENDING + 1))
                fi
            done
            if (( PENDING == 0 )); then
                ALL_ACTIVE=true
                break
            fi
            sleep 1
        done
        ACTIVE_NS=$(date +%s%N)
        DURATION_MS=$(( (ACTIVE_NS - RESTART_NS) / 1000000 ))
        if $ALL_ACTIVE; then
            echo "  All services active after: ${DURATION_MS} ms"
        else
            echo "  TIMEOUT: not all services active within 120s (${PENDING} pending, elapsed ${DURATION_MS} ms)"
        fi
    fi
    echo ""
    echo "=== Critical-chain analysis ==="
    systemd-analyze critical-chain greengrass-lite.target 2>&1 || echo "  (unavailable)"
} > "$STARTUP_LOG"
cat "$STARTUP_LOG"
echo

# Step 2: Smoke test gate
echo ">>> Running smoke tests..."
if ! "$SCRIPT_DIR/smoke-test.sh"; then
    echo "FATAL: Smoke tests failed. Aborting benchmark." >&2
    exit 1
fi
echo ">>> Smoke tests passed."
echo

# Step 2: Scenarios
echo ">>> Running baseline scenario..."
mkdir -p "$OUTPUT_DIR/baseline"
"$SCRIPT_DIR/scenarios/baseline.sh" "$OUTPUT_DIR/baseline/"
echo

echo ">>> Running simple-component scenario..."
mkdir -p "$OUTPUT_DIR/simple-component"
"$SCRIPT_DIR/scenarios/simple-component.sh" "$OUTPUT_DIR/simple-component/"
echo

echo ">>> Running realistic-load scenario..."
mkdir -p "$OUTPUT_DIR/realistic-load"
"$SCRIPT_DIR/scenarios/realistic-load.sh" "$OUTPUT_DIR/realistic-load/"
echo

# Phase 2: Cloud deployment scenarios (only if --phase2 flag is set)
if [[ "$RUN_PHASE2" == "true" ]]; then
    echo ">>> Running cloud-deploy-initial scenario..."
    mkdir -p "$OUTPUT_DIR/cloud-deploy-initial"
    "$SCRIPT_DIR/scenarios/cloud-deploy-initial.sh" "$OUTPUT_DIR/cloud-deploy-initial/" || \
        echo "WARNING: cloud-deploy-initial failed (non-fatal, continuing)"
    echo

    echo ">>> Running cloud-deploy-update scenario..."
    mkdir -p "$OUTPUT_DIR/cloud-deploy-update"
    "$SCRIPT_DIR/scenarios/cloud-deploy-update.sh" "$OUTPUT_DIR/cloud-deploy-update/" || \
        echo "WARNING: cloud-deploy-update failed (non-fatal, continuing)"
    echo
fi

# Step 3: Generate report
echo ">>> Generating report..."
"$SCRIPT_DIR/report-generator.sh" "$OUTPUT_DIR"
echo

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))
MINUTES=$((ELAPSED / 60))
SECONDS=$((ELAPSED % 60))

echo "============================================"
echo " Benchmark complete"
echo " Arch: $ARCH"
echo " Elapsed: ${MINUTES}m ${SECONDS}s"
echo " Results: $OUTPUT_DIR"
echo "============================================"
