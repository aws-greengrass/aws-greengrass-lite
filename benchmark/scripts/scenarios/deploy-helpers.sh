#!/bin/bash
# Shared deploy/undeploy helpers for scenario scripts.
# Source this file after setting COMPONENTS_DIR.

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
# Deploy a component with pre-staged artifacts.
# Usage: deploy_component <dir-name>
###############################################################################
deploy_component() {
  local dir_name="$1"
  local recipe_dir="${COMPONENTS_DIR}/${dir_name}"
  local component_name="${COMPONENT_NAMES[$dir_name]}"
  local version="1.0.0"
  local artifacts_dest="/var/lib/greengrass/packages/artifacts/${component_name}/${version}"

  if [[ ! -d "$recipe_dir" ]]; then
    echo "[ERROR] deploy_component: recipe dir not found: ${recipe_dir}" >&2
    return 1
  fi
  if [[ -z "$component_name" ]]; then
    echo "[ERROR] deploy_component: no mapping for: ${dir_name}" >&2
    return 1
  fi

  mkdir -p "$artifacts_dest"
  cp -r "${recipe_dir}/src" "$artifacts_dest/"
  chown -R root:ggcore "/var/lib/greengrass/packages/artifacts/${component_name}/"
  # ggl-cli may return non-zero due to cloud resolution errors (403) even though
  # the local deployment succeeds. Tolerate this for benchmarking purposes.
  ggl-cli deploy --recipe-dir "$recipe_dir" -c "${component_name}=${version}" || true
  sleep 5  # Allow deployment to process
}

###############################################################################
# Undeploy all mapped components (best-effort).
###############################################################################
undeploy_all_components() {
  for comp in "${COMPONENT_NAMES[@]}"; do
    ggl-cli deploy -d "$comp" 2>/dev/null || true
  done
}
