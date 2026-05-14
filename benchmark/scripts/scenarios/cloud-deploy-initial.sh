#!/bin/bash
set -euo pipefail

# cloud-deploy-initial.sh — Phase 2 scenario: initial cloud deployment via IoT Jobs
#
# Usage: cloud-deploy-initial.sh <output-dir>
#
# Triggers a cloud deployment of all 5 benchmark components via
# aws greengrassv2 create-deployment, polls device-side execution status via
# list-effective-deployments, and measures PSS/CPU/network/disk-I/O
# at 1-second granularity during the deployment window.
#
# Outputs in <output-dir>/:
#   deployment-timing.txt       — deployment_id, start/end ISO, duration_ms, status
#   deployment-timeseries.csv   — per-daemon PSS/RSS per second (from measure-deployment.sh)
#   cpu-deployment.csv          — CPU per second
#   network.csv                 — network throughput per second
#   diskio.csv                  — disk I/O per second
#   deployment-peaks.csv        — computed peak metrics
#
# Prerequisites:
#   - cloud-setup.env sourced with Phase 2 vars set
#   - AWS CLI configured with appropriate credentials
#   - GGLite running and connected to IoT Core

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=/dev/null
source "$SCRIPT_DIR/../cloud-setup.env"

OUTPUT_DIR="${1:?Usage: $0 <output-dir>}"
mkdir -p "$OUTPUT_DIR"

# --- Determine Thing Group ARN based on architecture ---
ARCH=$(uname -m)
case "$ARCH" in
    x86_64)  THING_GROUP_ARN="${GGL_THING_GROUP_ARN_X86_64:-}" ;;
    aarch64) THING_GROUP_ARN="${GGL_THING_GROUP_ARN_AARCH64:-}" ;;
    armv7l)  THING_GROUP_ARN="${GGL_THING_GROUP_ARN_ARMV7L:-}" ;;
    *) echo "ERROR: Unsupported architecture: $ARCH" >&2; exit 1 ;;
esac

if [[ -z "$THING_GROUP_ARN" ]]; then
    echo "ERROR: Thing Group ARN not set for arch=$ARCH. Check cloud-setup.env." >&2
    exit 1
fi

# --- Validate required env vars ---
: "${GGL_AWS_REGION:?GGL_AWS_REGION not set}"
: "${GGL_IOT_THING_NAME:?GGL_IOT_THING_NAME not set}"
: "${GGL_PHASE2_COMPONENT_HELLO_WORLD:?GGL_PHASE2_COMPONENT_HELLO_WORLD not set}"
: "${GGL_PHASE2_COMPONENT_IPC_PUBLISHER:?GGL_PHASE2_COMPONENT_IPC_PUBLISHER not set}"
: "${GGL_PHASE2_COMPONENT_IPC_SUBSCRIBER:?GGL_PHASE2_COMPONENT_IPC_SUBSCRIBER not set}"
: "${GGL_PHASE2_COMPONENT_IOT_CORE_PUBLISHER:?GGL_PHASE2_COMPONENT_IOT_CORE_PUBLISHER not set}"
: "${GGL_PHASE2_COMPONENT_S3_UPLOADER:?GGL_PHASE2_COMPONENT_S3_UPLOADER not set}"

COMPONENT_VERSION="${GGL_PHASE2_COMPONENT_VERSION:-1.0.0}"

# --- Cleanup trap: kill measure-deployment on error ---
MEASURE_PID=""
cleanup() {
    if [[ -n "$MEASURE_PID" ]]; then
        kill "$MEASURE_PID" 2>/dev/null || true
        wait "$MEASURE_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# --- Step 1: Clean up any previous local deployments of these components ---
echo "[cloud-deploy-initial] Cleaning up previous local deployments..."
for comp in "$GGL_PHASE2_COMPONENT_HELLO_WORLD" \
            "$GGL_PHASE2_COMPONENT_IPC_PUBLISHER" \
            "$GGL_PHASE2_COMPONENT_IPC_SUBSCRIBER" \
            "$GGL_PHASE2_COMPONENT_IOT_CORE_PUBLISHER" \
            "$GGL_PHASE2_COMPONENT_S3_UPLOADER"; do
    ggl-cli deploy -d "$comp" 2>/dev/null || true
done
sleep 5

# --- Step 2: Start measure-deployment.sh in background ---
echo "[cloud-deploy-initial] Starting deployment-window sampler (300s)..."
"$SCRIPT_DIR/../measure-deployment.sh" "$OUTPUT_DIR" --duration 300 &
MEASURE_PID=$!

# --- Step 3: Build components JSON and create deployment ---
COMPONENTS_JSON=$(cat <<EOF
{
  "${GGL_PHASE2_COMPONENT_HELLO_WORLD}": {"componentVersion": "${COMPONENT_VERSION}"},
  "${GGL_PHASE2_COMPONENT_IPC_PUBLISHER}": {"componentVersion": "${COMPONENT_VERSION}"},
  "${GGL_PHASE2_COMPONENT_IPC_SUBSCRIBER}": {"componentVersion": "${COMPONENT_VERSION}"},
  "${GGL_PHASE2_COMPONENT_IOT_CORE_PUBLISHER}": {"componentVersion": "${COMPONENT_VERSION}"},
  "${GGL_PHASE2_COMPONENT_S3_UPLOADER}": {"componentVersion": "${COMPONENT_VERSION}"}
}
EOF
)

DEPLOY_NAME="bench-phase2-initial-$(date +%s)"
echo "[cloud-deploy-initial] Creating deployment: $DEPLOY_NAME"
echo "[cloud-deploy-initial] Target: $THING_GROUP_ARN"
echo "[cloud-deploy-initial] Components: 5 at version $COMPONENT_VERSION"

DEPLOYMENT_START_NS=$(date +%s%N)
DEPLOYMENT_START_ISO=$(date -u +%Y-%m-%dT%H:%M:%SZ)

DEPLOYMENT_ID=$(aws greengrassv2 create-deployment \
    --target-arn "$THING_GROUP_ARN" \
    --deployment-name "$DEPLOY_NAME" \
    --components "$COMPONENTS_JSON" \
    --region "$GGL_AWS_REGION" \
    --query 'deploymentId' --output text 2>&1) || {
    echo "ERROR: create-deployment failed:" >&2
    echo "$DEPLOYMENT_ID" >&2
    exit 1
}

if [[ -z "$DEPLOYMENT_ID" || "$DEPLOYMENT_ID" == "None" ]]; then
    echo "ERROR: No deploymentId in response" >&2
    exit 1
fi
echo "[cloud-deploy-initial] Deployment ID: $DEPLOYMENT_ID"

# --- Step 4: Poll for device-side deployment completion via list-effective-deployments ---
echo "[cloud-deploy-initial] Polling device-side execution status (timeout 240s)..."
POLL_DEADLINE=$(( $(date +%s) + 240 ))
FINAL_STATUS="UNKNOWN"

while [[ $(date +%s) -lt $POLL_DEADLINE ]]; do
    sleep 5
    DEVICE_STATUS=$(aws greengrassv2 list-effective-deployments \
        --core-device-thing-name "$GGL_IOT_THING_NAME" \
        --region "$GGL_AWS_REGION" \
        --query "effectiveDeployments[?deploymentId==\`${DEPLOYMENT_ID}\`].coreDeviceExecutionStatus" \
        --output text 2>/dev/null) || continue

    echo "  Device status: ${DEVICE_STATUS:-PENDING}"

    case "$DEVICE_STATUS" in
        SUCCEEDED|FAILED|TIMED_OUT|CANCELED)
            FINAL_STATUS="$DEVICE_STATUS"
            break
            ;;
    esac
done

if [[ "$FINAL_STATUS" == "UNKNOWN" ]]; then
    echo "ERROR: Deployment timed out after 240s" >&2
    FINAL_STATUS="TIMEOUT"
fi

# --- Step 5: Record timing ---
DEPLOYMENT_END_NS=$(date +%s%N)
DEPLOYMENT_END_ISO=$(date -u +%Y-%m-%dT%H:%M:%SZ)
DURATION_MS=$(( (DEPLOYMENT_END_NS - DEPLOYMENT_START_NS) / 1000000 ))

{
    echo "deployment_id=$DEPLOYMENT_ID"
    echo "start_iso=$DEPLOYMENT_START_ISO"
    echo "end_iso=$DEPLOYMENT_END_ISO"
    echo "duration_ms=$DURATION_MS"
    echo "final_status=$FINAL_STATUS"
    echo "deployment_name=$DEPLOY_NAME"
    echo "arch=$ARCH"
    echo "component_version=$COMPONENT_VERSION"
} > "$OUTPUT_DIR/deployment-timing.txt"

echo "[cloud-deploy-initial] Deployment completed in ${DURATION_MS}ms (status: $FINAL_STATUS)"

# --- Step 6: Stop sampler and collect peaks ---
echo "[cloud-deploy-initial] Stopping sampler..."
kill "$MEASURE_PID" 2>/dev/null || true
wait "$MEASURE_PID" 2>/dev/null || true
MEASURE_PID=""

# --- Step 7: Post-deploy steady-state (5-min warmup + 10-min measurement) ---
if [[ "$FINAL_STATUS" == "TIMEOUT" ]]; then
    echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] Skipping post-deploy steady-state (deployment timed out)"
    echo "deployment timed out at $(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$OUTPUT_DIR/post-deploy-steady-skipped.txt"
else
    echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] Starting post-deploy steady-state (5-min warmup + 10-min measurement)..."
    mkdir -p "$OUTPUT_DIR/post-deploy-steady"
    if "$SCRIPT_DIR/../measure.sh" "$OUTPUT_DIR/post-deploy-steady" --duration 10 --warmup 5; then
        echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] Post-deploy steady-state complete → $OUTPUT_DIR/post-deploy-steady/"
    else
        echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] WARNING: Post-deploy steady-state measurement failed (non-fatal)"
    fi
fi

echo "[cloud-deploy-initial] Done."
echo "  Results: $OUTPUT_DIR"
echo "  NOTE: components remain deployed; run cloud-deploy-update.sh next or cancel via aws greengrassv2 cancel-deployment"
