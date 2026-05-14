#!/bin/bash
set -euo pipefail

# cloud-deploy-update.sh — Phase 2 scenario: version-bump cloud deployment
#
# Usage: cloud-deploy-update.sh <output-dir>
#
# Prerequisite: cloud-deploy-initial.sh has run successfully (components at 1.0.0).
#
# Simulates a real update by bumping hello-world to 1.0.1 while keeping
# the other 4 components pinned at 1.0.0. Polls device-side execution status
# via list-effective-deployments. Measures deployment-time resource
# consumption at 1-second granularity.
#
# Outputs in <output-dir>/:
#   deployment-timing.txt       — deployment_id, start/end ISO, duration_ms, status
#   deployment-timeseries.csv   — per-daemon PSS/RSS per second
#   cpu-deployment.csv          — CPU per second
#   network.csv                 — network throughput per second
#   diskio.csv                  — disk I/O per second
#   deployment-peaks.csv        — computed peak metrics

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
UPDATE_VERSION="1.0.1"

# --- Cleanup trap ---
MEASURE_PID=""
cleanup() {
    if [[ -n "$MEASURE_PID" ]]; then
        kill "$MEASURE_PID" 2>/dev/null || true
        wait "$MEASURE_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# --- Step 1: Register hello-world v1.0.1 (idempotent) ---
echo "[cloud-deploy-update] Registering ${GGL_PHASE2_COMPONENT_HELLO_WORLD} v${UPDATE_VERSION}..."

S3_BUCKET="${GGL_S3_BUCKET_NAME:?GGL_S3_BUCKET_NAME not set}"
S3_PREFIX="${GGL_PHASE2_S3_PREFIX:-benchmark-components}"

# Copy artifact from 1.0.0 to 1.0.1 in S3 (same binary, simulates version bump)
aws s3 cp \
    "s3://${S3_BUCKET}/${S3_PREFIX}/hello-world/${COMPONENT_VERSION}/" \
    "s3://${S3_BUCKET}/${S3_PREFIX}/hello-world/${UPDATE_VERSION}/" \
    --recursive --region "$GGL_AWS_REGION" 2>/dev/null || true

# Build updated recipe
RECIPE_DIR=$(mktemp -d /tmp/gg-phase2-update.XXXXXX)
cat > "$RECIPE_DIR/recipe-hello-${UPDATE_VERSION}.json" <<EOF
{
  "RecipeFormatVersion": "2020-01-25",
  "ComponentName": "${GGL_PHASE2_COMPONENT_HELLO_WORLD}",
  "ComponentVersion": "${UPDATE_VERSION}",
  "ComponentDescription": "Hello World benchmark component (updated)",
  "ComponentPublisher": "GGLite-Benchmark",
  "ComponentConfiguration": {
    "DefaultConfiguration": {
      "message": "Hello from Greengrass update!"
    }
  },
  "Manifests": [
    {
      "Platform": {"os": "linux"},
      "Lifecycle": {
        "run": "echo \"{configuration:/message}\""
      },
      "Artifacts": [
        {
          "URI": "s3://${S3_BUCKET}/${S3_PREFIX}/hello-world/${UPDATE_VERSION}/src.zip",
          "Unarchive": "ZIP"
        }
      ]
    }
  ]
}
EOF

# Register component version (skip if already exists)
if aws greengrassv2 describe-component \
    --arn "arn:aws:greengrass:${GGL_AWS_REGION}:$(aws sts get-caller-identity --query Account --output text):components:${GGL_PHASE2_COMPONENT_HELLO_WORLD}:versions:${UPDATE_VERSION}" \
    --region "$GGL_AWS_REGION" &>/dev/null; then
    echo "[cloud-deploy-update] Component version ${UPDATE_VERSION} already exists, skipping registration."
else
    aws greengrassv2 create-component-version \
        --inline-recipe "fileb://${RECIPE_DIR}/recipe-hello-${UPDATE_VERSION}.json" \
        --region "$GGL_AWS_REGION" || {
        echo "ERROR: Failed to register component version ${UPDATE_VERSION}" >&2
        rm -rf "$RECIPE_DIR"
        exit 1
    }
    echo "[cloud-deploy-update] Registered ${GGL_PHASE2_COMPONENT_HELLO_WORLD} v${UPDATE_VERSION}"
fi
rm -rf "$RECIPE_DIR"

# --- Step 2: Start measure-deployment.sh in background ---
echo "[cloud-deploy-update] Starting deployment-window sampler (180s)..."
"$SCRIPT_DIR/../measure-deployment.sh" "$OUTPUT_DIR" --duration 180 &
MEASURE_PID=$!

# --- Step 3: Create deployment with hello-world bumped to 1.0.1 ---
COMPONENTS_JSON=$(cat <<EOF
{
  "${GGL_PHASE2_COMPONENT_HELLO_WORLD}": {"componentVersion": "${UPDATE_VERSION}"},
  "${GGL_PHASE2_COMPONENT_IPC_PUBLISHER}": {"componentVersion": "${COMPONENT_VERSION}"},
  "${GGL_PHASE2_COMPONENT_IPC_SUBSCRIBER}": {"componentVersion": "${COMPONENT_VERSION}"},
  "${GGL_PHASE2_COMPONENT_IOT_CORE_PUBLISHER}": {"componentVersion": "${COMPONENT_VERSION}"},
  "${GGL_PHASE2_COMPONENT_S3_UPLOADER}": {"componentVersion": "${COMPONENT_VERSION}"}
}
EOF
)

DEPLOY_NAME="bench-phase2-update-$(date +%s)"
echo "[cloud-deploy-update] Creating deployment: $DEPLOY_NAME"
echo "[cloud-deploy-update] Bumping ${GGL_PHASE2_COMPONENT_HELLO_WORLD} ${COMPONENT_VERSION} → ${UPDATE_VERSION}"

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
echo "[cloud-deploy-update] Deployment ID: $DEPLOYMENT_ID"

# --- Step 4: Poll for device-side deployment completion via list-effective-deployments ---
echo "[cloud-deploy-update] Polling device-side execution status (timeout 120s)..."
POLL_DEADLINE=$(( $(date +%s) + 120 ))
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
    echo "ERROR: Deployment timed out after 120s" >&2
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
    echo "component_version_from=$COMPONENT_VERSION"
    echo "component_version_to=$UPDATE_VERSION"
} > "$OUTPUT_DIR/deployment-timing.txt"

echo "[cloud-deploy-update] Deployment completed in ${DURATION_MS}ms (status: $FINAL_STATUS)"

# --- Step 6: Stop sampler and collect peaks ---
echo "[cloud-deploy-update] Stopping sampler..."
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

echo "[cloud-deploy-update] Done."
echo "  Results: $OUTPUT_DIR"
