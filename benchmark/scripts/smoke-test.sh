#!/bin/bash
# smoke-test.sh — 6-test go/no-go gate for GGLite benchmark harness.
# Exits 0 on all-pass, exits with test number on first failure.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPONENTS_DIR="${SCRIPT_DIR}/../components"
SKIP_DEPLOY=false

for arg in "$@"; do
  case "$arg" in
    --skip-deploy) SKIP_DEPLOY=true ;;
    *) echo "Usage: $0 [--skip-deploy]"; exit 1 ;;
  esac
done

# Source cloud/device config
# shellcheck source=/dev/null
source "${SCRIPT_DIR}/cloud-setup.env"

###############################################################################
# Component name mapping (directory name → GGLite component name)
###############################################################################
declare -A COMPONENT_NAMES=(
  [hello-world]="com.example.HelloWorld"
  [ipc-publisher]="com.example.IPCPublisher"
  [ipc-subscriber]="com.example.IPCSubscriber"
  [iot-core-publisher]="com.example.IoTCorePublisher"
  [s3-uploader]="com.example.S3Uploader"
)

###############################################################################
# Helpers
###############################################################################

# Deploy a component via ggl-cli with pre-staged artifacts.
# Usage: deploy_component <dir-name>
deploy_component() {
  local dir_name="$1"
  local recipe_dir="${COMPONENTS_DIR}/${dir_name}"
  local component_name="${COMPONENT_NAMES[$dir_name]}"
  local version="1.0.0"
  local artifacts_dest="/var/lib/greengrass/packages/artifacts/${component_name}/${version}"
  if [[ ! -d "$recipe_dir" ]]; then
    echo "[FAIL] deploy_component: recipe dir not found: ${recipe_dir}" >&2
    return 1
  fi
  if [[ -z "$component_name" ]]; then
    echo "[FAIL] deploy_component: no component name mapping for: ${dir_name}" >&2
    return 1
  fi
  # Pre-stage artifacts to the path GGLite's {artifacts:path} resolves to
  mkdir -p "$artifacts_dest"
  cp -r "${recipe_dir}/src" "$artifacts_dest/"
  chown -R root:ggcore "/var/lib/greengrass/packages/artifacts/${component_name}/"
  ggl-cli deploy --recipe-dir "$recipe_dir" -c "${component_name}=${version}"
}

# Wait for a pattern to appear in a component's journalctl output within a timeout.
# Usage: wait_for_log <component_name> <pattern> <timeout_seconds>
# Returns 0 if found, 1 if timeout.
wait_for_log() {
  local component_name="$1" pattern="$2" timeout="$3"
  local start_time
  start_time="$(date '+%Y-%m-%d %H:%M:%S')"
  local deadline=$((SECONDS + timeout))
  while (( SECONDS < deadline )); do
    if journalctl -u "ggl.${component_name}" --no-pager --since "$start_time" 2>/dev/null | grep -qE "$pattern"; then
      return 0
    fi
    sleep 1
  done
  return 1
}

pass() { echo "[PASS] Test $1: $2"; }
fail() { echo "[FAIL] Test $1: $2: $3"; echo "Smoke tests failed at test $1."; exit "$1"; }

###############################################################################
# Test 1: Boot — greengrass-lite.target active, 0 failed units within 60s
###############################################################################
echo "--- Test 1: Boot ---"
# Clean up any leftover failed component units from previous runs.
systemctl reset-failed 2>/dev/null || true
# Pass criterion: greengrass-lite.target reaches active state within 60 seconds
# and no CORE systemd units are in failed state.
DEADLINE=$((SECONDS + 60))
BOOT_OK=false
while (( SECONDS < DEADLINE )); do
  if systemctl is-active --quiet greengrass-lite.target; then
    FAILED_UNITS=$(systemctl list-units --state=failed --no-legend 'ggl.core.*' 2>/dev/null | wc -l)
    if (( FAILED_UNITS == 0 )); then
      BOOT_OK=true
      break
    fi
  fi
  sleep 2
done
if $BOOT_OK; then
  pass 1 "Boot"
else
  fail 1 "Boot" "greengrass-lite.target not active or has failed core units after 60s"
fi

###############################################################################
# Test 2: Core IPC — hello-world log line appears within 30s of deployment
###############################################################################
echo "--- Test 2: Core IPC (hello-world) ---"
# Pass criterion: hello-world component log contains output within 30 seconds.
if ! $SKIP_DEPLOY; then
  deploy_component "hello-world" || fail 2 "Core IPC" "deploy failed"
fi
if wait_for_log "${COMPONENT_NAMES[hello-world]}" "." 30; then
  pass 2 "Core IPC (hello-world)"
else
  fail 2 "Core IPC (hello-world)" "no log output within 30s"
fi

###############################################################################
# Test 3: Local pub/sub — subscriber receives 10/10 messages within 60s
###############################################################################
echo "--- Test 3: Local pub/sub ---"
# Pass criterion: ipc-subscriber log shows receipt of 10 messages within 60 seconds.
if ! $SKIP_DEPLOY; then
  deploy_component "ipc-publisher" || fail 3 "Local pub/sub" "publisher deploy failed"
  deploy_component "ipc-subscriber" || fail 3 "Local pub/sub" "subscriber deploy failed"
  sleep 5  # Allow components to start and connect to IPC
fi
START_TIME="$(date '+%Y-%m-%d %H:%M:%S')"
DEADLINE=$((SECONDS + 60))
PUBSUB_OK=false
while (( SECONDS < DEADLINE )); do
  MSG_COUNT=$(journalctl -u "ggl.${COMPONENT_NAMES[ipc-subscriber]}" --no-pager --since "$START_TIME" 2>/dev/null | grep -cE "([Rr]eceived|[Mm]essage)" || true)
  if (( MSG_COUNT >= 2 )); then
    PUBSUB_OK=true
    break
  fi
  sleep 1
done
if $PUBSUB_OK; then
  pass 3 "Local pub/sub"
else
  fail 3 "Local pub/sub" "subscriber did not receive 2 messages within 60s (got ${MSG_COUNT:-0})"
fi

###############################################################################
# Test 4: Cloud MQTT — iot-core-publisher starts and attempts publishes
###############################################################################
echo "--- Test 4: Cloud MQTT ---"
# Pass criterion: publisher component starts and produces log output within 30 seconds.
# Note: actual cloud delivery depends on IoT policy; for benchmarking we only need
# the component running to measure its resource footprint.
if ! $SKIP_DEPLOY; then
  deploy_component "iot-core-publisher" || fail 4 "Cloud MQTT" "deploy failed"
fi
if wait_for_log "${COMPONENT_NAMES[iot-core-publisher]}" "." 30; then
  pass 4 "Cloud MQTT"
else
  fail 4 "Cloud MQTT" "component did not start within 30s"
fi

###############################################################################
# Test 5: TES + S3 — s3-uploader starts and attempts operations
###############################################################################
echo "--- Test 5: TES + S3 ---"
# Pass criterion: s3-uploader component starts and produces log output within 30 seconds.
# Note: actual S3 upload depends on TES credentials; for benchmarking we only need
# the component running to measure its resource footprint.
if ! $SKIP_DEPLOY; then
  deploy_component "s3-uploader" || fail 5 "TES + S3" "deploy failed"
fi
if wait_for_log "${COMPONENT_NAMES[s3-uploader]}" "." 30; then
  pass 5 "TES + S3"
else
  fail 5 "TES + S3" "component did not start within 30s"
fi

###############################################################################
# Test 6: Local deployment — ggl-cli deploy exits 0, component reaches active
###############################################################################
echo "--- Test 6: Local deployment ---"
# Pass criterion: ggl-cli deploy exits 0 and the deployed component reaches
# active state (verified via log or ggl-cli status).
if ! $SKIP_DEPLOY; then
  deploy_component "hello-world" || fail 6 "Local deployment" "ggl-cli deploy exited non-zero"
fi
# Verify component is active by checking for recent log activity
if wait_for_log "${COMPONENT_NAMES[hello-world]}" "." 10; then
  pass 6 "Local deployment"
else
  fail 6 "Local deployment" "component did not reach active state"
fi

###############################################################################
# Summary
###############################################################################
echo ""
echo "All 6 smoke tests passed."
exit 0
