#!/usr/bin/env bash
# provision-device.sh — Install GGLite v2.5.0 on a target device via SSH.
#
# Usage: ./provision-device.sh <device-ip> <arch>
#   arch: x86_64 | aarch64 | armv7l
#
# Requires cloud-setup.env in the same directory as this script.
set -euo pipefail

# --- Constants -----------------------------------------------------------
readonly GGLITE_VERSION="2.5.0"
readonly GGLITE_RELEASE_URL="https://github.com/aws-greengrass/aws-greengrass-lite/releases/download/v${GGLITE_VERSION}"
readonly ROOT_CA_URL="https://www.amazontrust.com/repository/AmazonRootCA1.pem"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR
readonly WORK_DIR="/tmp/gglite-provision-$$"

# On-device paths for credentials and config
readonly DEVICE_ROOT_PATH="/var/lib/greengrass"
readonly DEVICE_CERT_PATH="${DEVICE_ROOT_PATH}/device.pem"
readonly DEVICE_KEY_PATH="${DEVICE_ROOT_PATH}/device.key"
readonly DEVICE_ROOT_CA_PATH="${DEVICE_ROOT_PATH}/AmazonRootCA1.pem"
readonly DEVICE_CONFIG_PATH="/etc/greengrass/config.yaml"

# The extracted .deb has a generic name regardless of architecture
readonly EXTRACTED_DEB_NAME="aws-greengrass-lite-${GGLITE_VERSION}-Linux.deb"

# --- Color helpers -------------------------------------------------------
if [[ -t 1 ]]; then
    readonly C_GREEN=$'\033[0;32m'
    readonly C_YELLOW=$'\033[0;33m'
    readonly C_RED=$'\033[0;31m'
    readonly C_RESET=$'\033[0m'
else
    readonly C_GREEN="" C_YELLOW="" C_RED="" C_RESET=""
fi

log()   { echo "${C_GREEN}[provision-device]${C_RESET} $*"; }
warn()  { echo "${C_YELLOW}[provision-device] WARN:${C_RESET} $*" >&2; }
error() { echo "${C_RED}[provision-device] ERROR:${C_RESET} $*" >&2; exit 1; }

# --- Cleanup trap --------------------------------------------------------
# shellcheck disable=SC2317 # Called via trap, not unreachable
cleanup() {
    if [[ -d "${WORK_DIR}" ]]; then
        rm -rf "${WORK_DIR}"
        log "Cleaned up ${WORK_DIR}"
    fi
}
trap cleanup EXIT

# --- Argument validation -------------------------------------------------
if [[ $# -lt 2 ]]; then
    error "Usage: $0 <device-ip> <arch>  (arch: x86_64|aarch64|armv7l)"
fi

readonly DEVICE_IP="$1"
readonly ARCH="$2"

# Map architecture to zip suffix used in the GitHub release assets.
# The v2.5.0 release publishes zip files (not bare .debs):
#   aws-greengrass-lite-deb-x86-64.zip, -arm64.zip, -armv7.zip
# Each zip contains a generically-named .deb: aws-greengrass-lite-2.5.0-Linux.deb
map_arch_to_zip_suffix() {
    case "$1" in
        x86_64)  echo "x86-64" ;;
        aarch64) echo "arm64" ;;
        armv7l)  echo "armv7" ;;
        *)       error "Unsupported architecture: $1 (expected x86_64|aarch64|armv7l)" ;;
    esac
}

ZIP_SUFFIX="$(map_arch_to_zip_suffix "${ARCH}")"
readonly ZIP_SUFFIX
readonly ZIP_FILENAME="aws-greengrass-lite-deb-${ZIP_SUFFIX}.zip"
readonly ZIP_URL="${GGLITE_RELEASE_URL}/${ZIP_FILENAME}"

# --- Load environment variables ------------------------------------------
readonly ENV_FILE="${SCRIPT_DIR}/cloud-setup.env"
if [[ ! -f "${ENV_FILE}" ]]; then
    error "Missing ${ENV_FILE}. Copy cloud-setup.env.example to cloud-setup.env and fill in values."
fi

# shellcheck source=/dev/null
source "${ENV_FILE}"

# Validate required env vars
validate_env() {
    local var_name="$1"
    local var_value="${!var_name:-}"
    if [[ -z "${var_value}" || "${var_value}" == "<REPLACE_ME>" ]]; then
        error "Required variable ${var_name} is not set in ${ENV_FILE}"
    fi
}

validate_env "GGL_AWS_REGION"
validate_env "GGL_IOT_THING_NAME"
validate_env "GGL_IOT_CRED_ENDPOINT"
validate_env "GGL_IOT_DATA_ENDPOINT"
validate_env "GGL_IOT_ROLE_ALIAS"
validate_env "GGL_S3_BUCKET_NAME"
validate_env "GGL_CERT_PATH"
validate_env "GGL_PRIVATE_KEY_PATH"

# Validate cert/key files exist locally
[[ -f "${GGL_CERT_PATH}" ]] || error "Certificate file not found: ${GGL_CERT_PATH}"
[[ -f "${GGL_PRIVATE_KEY_PATH}" ]] || error "Private key file not found: ${GGL_PRIVATE_KEY_PATH}"

# --- SSH configuration ---------------------------------------------------
# SSH host key verification is disabled: benchmark targets are throwaway EC2s or
# lab-controlled devices (RPi 3/4) on trusted networks, re-imaged per run. Do NOT
# use this pattern for production provisioning — it is vulnerable to MITM on
# untrusted paths. For production, use known_hosts pinning or SSM Session Manager.
readonly SSH_USER="${GGL_SSH_USER:-ubuntu}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10)
if [[ -n "${GGL_SSH_KEY:-}" ]]; then
    SSH_OPTS+=(-i "${GGL_SSH_KEY}")
fi

# shellcheck disable=SC2029 # Intentional client-side expansion for ssh commands
ssh_cmd() { ssh "${SSH_OPTS[@]}" "${SSH_USER}@${DEVICE_IP}" "$@"; }
scp_cmd() { scp "${SSH_OPTS[@]}" "$@"; }

# --- Prepare working directory -------------------------------------------
mkdir -p "${WORK_DIR}"
log "Working directory: ${WORK_DIR}"

# --- Download and extract .deb from zip ----------------------------------
log "Downloading ${ZIP_FILENAME}..."
if ! curl -fSL --retry 3 -o "${WORK_DIR}/${ZIP_FILENAME}" "${ZIP_URL}"; then
    error "Failed to download ${ZIP_URL}"
fi
log "Downloaded ${ZIP_FILENAME} ($(du -h "${WORK_DIR}/${ZIP_FILENAME}" | cut -f1))"

log "Extracting .deb from zip..."
unzip -o -q "${WORK_DIR}/${ZIP_FILENAME}" -d "${WORK_DIR}"
if [[ ! -f "${WORK_DIR}/${EXTRACTED_DEB_NAME}" ]]; then
    error "Expected ${EXTRACTED_DEB_NAME} not found after unzip. Contents: $(ls "${WORK_DIR}")"
fi
log "Extracted ${EXTRACTED_DEB_NAME}"

# --- Handle Root CA ------------------------------------------------------
if [[ -z "${GGL_ROOT_CA_PATH:-}" || ! -f "${GGL_ROOT_CA_PATH:-}" ]]; then
    log "AmazonRootCA1.pem not found locally; downloading..."
    curl -fSL --retry 3 -o "${WORK_DIR}/AmazonRootCA1.pem" "${ROOT_CA_URL}"
    ROOT_CA_LOCAL="${WORK_DIR}/AmazonRootCA1.pem"
else
    ROOT_CA_LOCAL="${GGL_ROOT_CA_PATH}"
fi

# --- Render config.yaml --------------------------------------------------
log "Rendering config.yaml..."
cat > "${WORK_DIR}/config.yaml" <<EOF
---
system:
  privateKeyPath: "${DEVICE_KEY_PATH}"
  certificateFilePath: "${DEVICE_CERT_PATH}"
  rootCaPath: "${DEVICE_ROOT_CA_PATH}"
  rootPath: "${DEVICE_ROOT_PATH}"
  thingName: "${GGL_IOT_THING_NAME}"
services:
  aws.greengrass.NucleusLite:
    componentType: "NUCLEUS"
    configuration:
      awsRegion: "${GGL_AWS_REGION}"
      iotCredEndpoint: "${GGL_IOT_CRED_ENDPOINT}"
      iotDataEndpoint: "${GGL_IOT_DATA_ENDPOINT}"
      iotRoleAlias: "${GGL_IOT_ROLE_ALIAS}"
      runWithDefault:
        posixUser: "gg_component:gg_component"
      greengrassDataPlanePort: "8443"
      platformOverride: {}
EOF

# --- Transfer files to device --------------------------------------------
log "Transferring files to ${DEVICE_IP}..."
scp_cmd \
    "${WORK_DIR}/${EXTRACTED_DEB_NAME}" \
    "${GGL_CERT_PATH}" \
    "${GGL_PRIVATE_KEY_PATH}" \
    "${ROOT_CA_LOCAL}" \
    "${WORK_DIR}/config.yaml" \
    "${SSH_USER}@${DEVICE_IP}:/tmp/"

# --- Remote installation -------------------------------------------------
# Execution order:
#   (a) Install apt runtime deps
#   (b) dpkg -i the .deb (postinst creates ggcore/gg_component users, enables units)
#   (c) Place cert/key/config and chown (users now exist from postinst)
#   (d) systemctl start
#
# NOTE: We do NOT pre-create ggcore or gg_component users/groups. The .deb's
# postinst handles user creation with `useradd -m`. Pre-creating them with
# different flags (e.g., -r/no-home) causes postinst to fail with exit 9,
# leaving the package unconfigured. See: VM validation 2026-05-05.
log "Installing GGLite on device..."
ssh_cmd sudo bash <<REMOTE_SCRIPT
set -euo pipefail

# --- (a) Install runtime prerequisites ---
echo "[remote] Installing prerequisites..."
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq \
    libcurl4 \
    libevent-2.1-7 \
    libsystemd0 \
    liburiparser1 \
    uuid-runtime \
    libyaml-0-2 \
    libzip4 \
    openssl \
    libsqlite3-0 \
    cgroup-tools \
    unzip \
    systemd \
    > /dev/null

# --- (b) Install the .deb (creates users + enables systemd units) ---
echo "[remote] Installing .deb package..."
if ! dpkg -i "/tmp/${EXTRACTED_DEB_NAME}"; then
    echo "[remote] dpkg reported issues; running apt-get -f install to fix..."
    apt-get -f install -y -qq
    dpkg --configure -a
fi

# --- (c) Place cert, key, root CA, and config (users exist now) ---
echo "[remote] Placing credentials and config..."
mkdir -p "${DEVICE_ROOT_PATH}"
mkdir -p /etc/greengrass

cp "/tmp/$(basename "${GGL_CERT_PATH}")" "${DEVICE_CERT_PATH}"
cp "/tmp/$(basename "${GGL_PRIVATE_KEY_PATH}")" "${DEVICE_KEY_PATH}"
cp "/tmp/$(basename "${ROOT_CA_LOCAL}")" "${DEVICE_ROOT_CA_PATH}"

chown ggcore:ggcore "${DEVICE_CERT_PATH}" "${DEVICE_KEY_PATH}" "${DEVICE_ROOT_CA_PATH}"
chmod 600 "${DEVICE_CERT_PATH}" "${DEVICE_KEY_PATH}" "${DEVICE_ROOT_CA_PATH}"

cp /tmp/config.yaml "${DEVICE_CONFIG_PATH}"
chown root:ggcore "${DEVICE_CONFIG_PATH}"
chmod 644 "${DEVICE_CONFIG_PATH}"

# --- (d) Start the service ---
echo "[remote] Starting greengrass-lite.target..."
systemctl daemon-reload
systemctl enable greengrass-lite.target
systemctl start greengrass-lite.target
REMOTE_SCRIPT

# --- Poll for service readiness ------------------------------------------
log "Waiting for greengrass-lite.target to become active (up to 60s)..."
readonly POLL_TIMEOUT=60
readonly POLL_INTERVAL=5
elapsed=0

while (( elapsed < POLL_TIMEOUT )); do
    if ssh_cmd systemctl is-active greengrass-lite.target 2>/dev/null | grep -q "^active$"; then
        log "greengrass-lite.target is ACTIVE"
        echo ""
        log "=== Service Status ==="
        ssh_cmd systemctl status --no-pager greengrass-lite.target || true
        echo ""
        log "=== Failed Units (should be 0) ==="
        ssh_cmd systemctl list-units --state=failed --no-pager --no-legend || true
        echo ""
        log "Provisioning complete for ${ARCH} device at ${DEVICE_IP}"
        exit 0
    fi
    sleep "${POLL_INTERVAL}"
    elapsed=$(( elapsed + POLL_INTERVAL ))
    log "  ...waiting (${elapsed}s/${POLL_TIMEOUT}s)"
done

warn "greengrass-lite.target did not become active within ${POLL_TIMEOUT}s"
log "Current status:"
ssh_cmd systemctl status --no-pager greengrass-lite.target || true
ssh_cmd journalctl -u greengrass-lite.target --no-pager -n 30 || true
exit 1
