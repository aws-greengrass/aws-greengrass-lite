#!/bin/bash
# pull-data.sh — Pull benchmark raw data from a remote device back to the host.
#
# Usage: ./pull-data.sh <arch> <device-ssh-target> [<remote-data-dir>]
#   arch:               aarch64 | x86_64 | armv7l
#   device-ssh-target:  e.g. pi@192.168.68.15, or an SSH alias
#                       Use SSH config (~/.ssh/config) for bastion/jump hosts.
#   remote-data-dir:    Remote path to benchmark/data/<arch>/ (default: ~/benchmark/data)
#
# Example:
#   ./pull-data.sh aarch64 pi@192.168.68.15
#
# The host-side benchmark/data/<arch>/<YYYY-MM-DD>/ directory is gitignored
# but kept locally so runs are auditable and reproducible.
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <arch> <device-ssh-target> [<remote-data-dir>]" >&2
    echo "  arch:              aarch64 | x86_64 | armv7l" >&2
    echo "  device-ssh-target: e.g. pi@192.168.68.15 or an SSH config alias" >&2
    echo "  remote-data-dir:   Remote path to benchmark/data (default: ~/benchmark/data)" >&2
    exit 1
fi

ARCH="$1"
DEVICE="$2"
REMOTE_DATA_DIR="${3:-~/benchmark/data}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HOST_DATA_DIR="$(cd "$SCRIPT_DIR/.." && pwd)/data/$ARCH"
mkdir -p "$HOST_DATA_DIR"

echo "Pulling ${ARCH} data from ${DEVICE}:${REMOTE_DATA_DIR}/${ARCH}/ to ${HOST_DATA_DIR}/"

# Prefer rsync if available (efficient deltas). Fall back to scp -r.
if command -v rsync >/dev/null 2>&1; then
    rsync -avz --progress "${DEVICE}:${REMOTE_DATA_DIR}/${ARCH}/" "${HOST_DATA_DIR}/"
else
    scp -r "${DEVICE}:${REMOTE_DATA_DIR}/${ARCH}/*" "${HOST_DATA_DIR}/"
fi

echo ""
echo "Pulled files:"
find "$HOST_DATA_DIR" -type f | sort
