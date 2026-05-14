#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export COMPONENTS_DIR="$SCRIPT_DIR/../../components"

# shellcheck source=/dev/null
source "$SCRIPT_DIR/../cloud-setup.env"
# shellcheck source=deploy-helpers.sh
source "$SCRIPT_DIR/deploy-helpers.sh"

OUTPUT_DIR="${1:?Usage: $0 <output-dir>}"
mkdir -p "$OUTPUT_DIR"

# Remove any non-core components for a clean slate
cleanup() {
    local components
    components=$(ggl-cli list-components 2>/dev/null | grep -v '^\(aws\.greengrass\|ggl\.\)' || true)
    for comp in $components; do
        ggl-cli deploy -d "$comp" 2>/dev/null || true
    done
}

trap cleanup EXIT

# Clean slate before measurement
cleanup

# Measure idle daemons
"$SCRIPT_DIR/../measure.sh" "$OUTPUT_DIR"
