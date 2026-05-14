#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export COMPONENTS_DIR="$SCRIPT_DIR/../../components"

# shellcheck source=/dev/null
source "$SCRIPT_DIR/../cloud-setup.env"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/deploy-helpers.sh"

OUTPUT_DIR="${1:?Usage: $0 <output-dir>}"
mkdir -p "$OUTPUT_DIR"

# GGLite does not support multiple instances of the same component.
# Deploy each component once; message rate is determined by component config.
DEPLOY_LIST=(ipc-publisher ipc-subscriber iot-core-publisher s3-uploader)

cleanup() {
    for dir in "${DEPLOY_LIST[@]}"; do
        ggl-cli deploy -d "${COMPONENT_NAMES[$dir]}" 2>/dev/null || true
    done
}

trap cleanup EXIT

# Deploy components with pre-staged artifacts
for dir in "${DEPLOY_LIST[@]}"; do
    deploy_component "$dir"
done

# Wait for deployments to settle
sleep 10

# Measure
"$SCRIPT_DIR/../measure.sh" "$OUTPUT_DIR"
