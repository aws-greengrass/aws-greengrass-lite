#!/usr/bin/env bash
# provision-account.sh — Idempotent provisioning of AWS account-side resources
# for the GGLite benchmark harness (Phase 1 + Phase 2).
#
# Purpose:
#   Creates (or confirms existence of) all shared AWS resources needed by the
#   benchmark harness: IAM role + inline policies, IoT role alias, IoT device
#   policy, per-architecture IoT Thing Groups, and the S3 bucket. Designed to
#   be run once per account, but safe to re-run at any time — a second
#   invocation on an already-provisioned account exits 0 with no mutations.
#
# Idempotency contract:
#   Every resource uses a get-before-create pattern. Inline policies use
#   put-role-policy (inherently idempotent). The IoT policy uses a normalized
#   JSON comparison to avoid creating spurious versions.
#
# When to re-run:
#   - After updating the desired IoT policy document in this script
#   - When setting up a new AWS account for benchmarking
#   - Before Slices 9/10 on a fresh device to ensure Gap A/B are applied
#
# Required AWS permissions:
#   iam:GetRole, iam:CreateRole, iam:PutRolePolicy
#   iot:DescribeRoleAlias, iot:CreateRoleAlias
#   iot:GetPolicy, iot:CreatePolicy, iot:ListPolicyVersions,
#   iot:GetPolicyVersion, iot:CreatePolicyVersion, iot:SetDefaultPolicyVersion,
#   iot:DeletePolicyVersion
#   iot:DescribeThingGroup, iot:CreateThingGroup
#   s3:HeadBucket, s3:CreateBucket, s3:PutBucketVersioning,
#   s3:PutPublicAccessBlock, s3:PutObject
#   sts:GetCallerIdentity
#
# Usage:
#   ./provision-account.sh [--region REGION] [--account-id ACCOUNT_ID] [--dry-run]
#
# Example:
#   ./provision-account.sh --region us-west-2
#
set -euo pipefail

# --- Color helpers -------------------------------------------------------
if [[ -t 1 ]]; then
    readonly C_GREEN=$'\033[0;32m'
    readonly C_YELLOW=$'\033[0;33m'
    readonly C_RED=$'\033[0;31m'
    readonly C_CYAN=$'\033[0;36m'
    readonly C_RESET=$'\033[0m'
else
    readonly C_GREEN="" C_YELLOW="" C_RED="" C_CYAN="" C_RESET=""
fi

log()   { echo "${C_GREEN}[provision-account]${C_RESET} $*"; }
warn()  { echo "${C_YELLOW}[provision-account] WARN:${C_RESET} $*" >&2; }
error() { echo "${C_RED}[provision-account] ERROR:${C_RESET} $*" >&2; exit 1; }

# --- Dependency check ----------------------------------------------------
command -v jq >/dev/null 2>&1 || error "jq is required but not found. Install: apt-get install jq"
command -v aws >/dev/null 2>&1 || error "aws CLI is required but not found."

# --- Argument parsing ----------------------------------------------------
REGION="us-west-2"
ACCOUNT_ID=""
DRY_RUN=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --region)      REGION="$2"; shift 2 ;;
        --account-id)  ACCOUNT_ID="$2"; shift 2 ;;
        --dry-run)     DRY_RUN=true; shift ;;
        -h|--help)
            echo "Usage: $0 [--region REGION] [--account-id ACCOUNT_ID] [--dry-run]"
            exit 0 ;;
        *) error "Unknown argument: $1" ;;
    esac
done

# --- Infer account ID if not provided ------------------------------------
if [[ -z "${ACCOUNT_ID}" ]]; then
    ACCOUNT_ID="$(aws sts get-caller-identity --output json | jq -r '.Account')"
    [[ -n "${ACCOUNT_ID}" && "${ACCOUNT_ID}" != "null" ]] || error "Failed to infer account ID from sts get-caller-identity"
fi
readonly REGION ACCOUNT_ID DRY_RUN

log "Region: ${REGION} | Account: ${ACCOUNT_ID} | Dry-run: ${DRY_RUN}"

# --- Temp directory + cleanup trap ---------------------------------------
WORK_DIR="$(mktemp -d)"
readonly WORK_DIR
# shellcheck disable=SC2317
cleanup() { rm -rf "${WORK_DIR}"; }
trap cleanup EXIT

# --- Resource names ------------------------------------------------------
readonly IAM_ROLE_NAME="GGLite-Benchmark-TokenExchangeRole"
readonly IAM_POLICY_S3="GGLite-Benchmark-TES-Policy"
readonly IAM_POLICY_GG="GGLite-Benchmark-TES-GreengrassAccess"
readonly IOT_ROLE_ALIAS="GGLite-Benchmark-TokenExchangeRoleAlias"
readonly IOT_POLICY_NAME="GGLite-Benchmark-Policy"
readonly IOT_POLICY_DEVICE_NAME="GGLite-Benchmark-Device-01"
readonly THING_GROUPS=("gg-benchmark-x86_64" "gg-benchmark-aarch64" "gg-benchmark-armv7l")
readonly S3_BUCKET="gglite-benchmark-${ACCOUNT_ID}-${REGION}"

# --- Summary tracking ----------------------------------------------------
declare -A SUMMARY=()
record() { SUMMARY["$1"]="$2"; }

# --- Helpers -------------------------------------------------------------
run_or_dry() {
    if [[ "${DRY_RUN}" == "true" ]]; then
        echo "${C_CYAN}[dry-run]${C_RESET} $*"
        return 0
    fi
    "$@"
}

aws_check_iam_role_exists() {
    aws iam get-role --role-name "$1" --output json 2>/dev/null && return 0
    return 1
}

aws_check_iot_role_alias_exists() {
    aws iot describe-role-alias --role-alias "$1" --region "${REGION}" --output json 2>/dev/null && return 0
    return 1
}

aws_check_iot_policy_exists() {
    aws iot get-policy --policy-name "$1" --region "${REGION}" --output json 2>/dev/null && return 0
    return 1
}

aws_check_iot_thing_group_exists() {
    aws iot describe-thing-group --thing-group-name "$1" --region "${REGION}" --output json 2>/dev/null && return 0
    return 1
}

aws_check_s3_bucket_exists() {
    aws s3api head-bucket --bucket "$1" --region "${REGION}" 2>/dev/null && return 0
    return 1
}

# Returns 0 if the live default policy document differs from desired, 1 if identical.
aws_iot_policy_doc_differs() {
    local policy_name="$1"
    local desired_file="$2"

    local policy_info
    policy_info="$(aws iot get-policy --policy-name "${policy_name}" --region "${REGION}" --output json)"
    local default_version
    default_version="$(echo "${policy_info}" | jq -r '.defaultVersionId')"

    local live_doc
    live_doc="$(aws iot get-policy-version --policy-name "${policy_name}" \
        --policy-version-id "${default_version}" --region "${REGION}" --output json \
        | jq -r '.policyDocument')"

    # Normalize both for comparison (sorted keys, compact)
    local live_normalized desired_normalized
    live_normalized="$(echo "${live_doc}" | jq -S '.')"
    desired_normalized="$(jq -S '.' < "${desired_file}")"

    if [[ "${live_normalized}" == "${desired_normalized}" ]]; then
        return 1  # identical
    fi
    return 0  # differs
}

# Prune oldest non-default IoT policy versions if at max (5 versions limit).
iot_policy_prune_versions() {
    local policy_name="$1"
    local versions
    versions="$(aws iot list-policy-versions --policy-name "${policy_name}" \
        --region "${REGION}" --output json | jq -r '.policyVersions[] | select(.isDefaultVersion == false) | .versionId' | sort -V)"
    local count
    count="$(echo "${versions}" | grep -c . || true)"
    while (( count >= 4 )); do
        local oldest
        oldest="$(echo "${versions}" | head -1)"
        log "  Pruning old policy version: ${oldest}"
        run_or_dry aws iot delete-policy-version --policy-name "${policy_name}" \
            --policy-version-id "${oldest}" --region "${REGION}"
        versions="$(echo "${versions}" | tail -n +2)"
        count="$(echo "${versions}" | grep -c . || true)"
    done
}

# ==========================================================================
# (a) IAM Role
# ==========================================================================
log "--- IAM Role: ${IAM_ROLE_NAME} ---"
if aws_check_iam_role_exists "${IAM_ROLE_NAME}" >/dev/null; then
    log "  Exists"
    record "IAM Role" "exists"
else
    log "  Creating..."
    cat > "${WORK_DIR}/trust-policy.json" <<'EOF'
{"Version":"2012-10-17","Statement":[{"Effect":"Allow","Principal":{"Service":"credentials.iot.amazonaws.com"},"Action":"sts:AssumeRole"}]}
EOF
    run_or_dry aws iam create-role \
        --role-name "${IAM_ROLE_NAME}" \
        --assume-role-policy-document "file://${WORK_DIR}/trust-policy.json" \
        --output json >/dev/null
    record "IAM Role" "created"
fi

# ==========================================================================
# (b) IAM inline policy: S3 + CloudWatch Logs
# ==========================================================================
log "--- IAM Inline Policy: ${IAM_POLICY_S3} ---"
cat > "${WORK_DIR}/s3-policy.json" <<EOF
{"Version":"2012-10-17","Statement":[{"Sid":"S3BenchmarkBucket","Effect":"Allow","Action":["s3:PutObject","s3:GetObject","s3:ListBucket"],"Resource":["arn:aws:s3:::${S3_BUCKET}","arn:aws:s3:::${S3_BUCKET}/*"]},{"Sid":"CloudWatchLogs","Effect":"Allow","Action":["logs:CreateLogGroup","logs:CreateLogStream","logs:PutLogEvents"],"Resource":"arn:aws:logs:${REGION}:${ACCOUNT_ID}:log-group:/greengrass/benchmark*"}]}
EOF
run_or_dry aws iam put-role-policy \
    --role-name "${IAM_ROLE_NAME}" \
    --policy-name "${IAM_POLICY_S3}" \
    --policy-document "file://${WORK_DIR}/s3-policy.json"
record "IAM Policy (S3)" "updated"

# ==========================================================================
# (c) IAM inline policy: Greengrass access (Gap A)
# ==========================================================================
log "--- IAM Inline Policy: ${IAM_POLICY_GG} ---"
cat > "${WORK_DIR}/gg-policy.json" <<'EOF'
{"Version":"2012-10-17","Statement":[{"Sid":"GreengrassDataPlane","Effect":"Allow","Action":["greengrass:*","iot:DescribeCertificate","iot:DescribeThing","iot:ListThingGroupsForCoreDevice"],"Resource":"*"}]}
EOF
run_or_dry aws iam put-role-policy \
    --role-name "${IAM_ROLE_NAME}" \
    --policy-name "${IAM_POLICY_GG}" \
    --policy-document "file://${WORK_DIR}/gg-policy.json"
record "IAM Policy (Greengrass)" "updated"

# ==========================================================================
# (d) IoT Role Alias
# ==========================================================================
log "--- IoT Role Alias: ${IOT_ROLE_ALIAS} ---"
if aws_check_iot_role_alias_exists "${IOT_ROLE_ALIAS}" >/dev/null; then
    log "  Exists"
    record "IoT Role Alias" "exists"
else
    log "  Creating..."
    run_or_dry aws iot create-role-alias \
        --role-alias "${IOT_ROLE_ALIAS}" \
        --role-arn "arn:aws:iam::${ACCOUNT_ID}:role/${IAM_ROLE_NAME}" \
        --region "${REGION}" --output json >/dev/null
    record "IoT Role Alias" "created"
fi

# ==========================================================================
# (e) IoT Policy (Gap B — full Phase 2 document)
# ==========================================================================
log "--- IoT Policy: ${IOT_POLICY_NAME} ---"
# IoT policy for the benchmark device.
# Device-specific topic filters use ${IOT_POLICY_DEVICE_NAME} for future
# parameterization (change to a wildcard pattern for multi-device setups).
cat > "${WORK_DIR}/iot-policy.json" <<EOF
{"Version":"2012-10-17","Statement":[{"Sid":"GreengrassConnect","Effect":"Allow","Action":"iot:Connect","Resource":"arn:aws:iot:${REGION}:${ACCOUNT_ID}:client/*"},{"Sid":"GreengrassPublish","Effect":"Allow","Action":"iot:Publish","Resource":["arn:aws:iot:${REGION}:${ACCOUNT_ID}:topic/\$aws/things/${IOT_POLICY_DEVICE_NAME}/*","arn:aws:iot:${REGION}:${ACCOUNT_ID}:topic/greengrass/*"]},{"Sid":"GreengrassSubscribe","Effect":"Allow","Action":"iot:Subscribe","Resource":["arn:aws:iot:${REGION}:${ACCOUNT_ID}:topicfilter/\$aws/things/${IOT_POLICY_DEVICE_NAME}/*","arn:aws:iot:${REGION}:${ACCOUNT_ID}:topicfilter/greengrass/*"]},{"Sid":"GreengrassReceive","Effect":"Allow","Action":"iot:Receive","Resource":["arn:aws:iot:${REGION}:${ACCOUNT_ID}:topic/\$aws/things/${IOT_POLICY_DEVICE_NAME}/*","arn:aws:iot:${REGION}:${ACCOUNT_ID}:topic/greengrass/*"]},{"Sid":"AssumeRoleWithCertificate","Effect":"Allow","Action":"iot:AssumeRoleWithCertificate","Resource":"arn:aws:iot:${REGION}:${ACCOUNT_ID}:rolealias/${IOT_ROLE_ALIAS}"},{"Sid":"GreengrassDataPlane","Effect":"Allow","Action":["greengrass:GetComponentVersionArtifact","greengrass:ResolveComponentCandidates","greengrass:GetDeploymentConfiguration","greengrass:ListThingGroupsForCoreDevice","greengrass:PutCoreDevice","greengrass:UpdateCoreDevice","greengrass:GetCoreDevice","greengrass:GetConnectivityInfo","greengrass:UpdateConnectivityInfo","greengrass:VerifyClientDeviceIdentity","greengrass:VerifyClientDeviceIoTCertificateAssociation","greengrass:GetThingConnectivityInfo","greengrass:Discover"],"Resource":"*"}]}
EOF

if aws_check_iot_policy_exists "${IOT_POLICY_NAME}" >/dev/null; then
    if aws_iot_policy_doc_differs "${IOT_POLICY_NAME}" "${WORK_DIR}/iot-policy.json"; then
        log "  Policy exists but document differs — updating..."
        iot_policy_prune_versions "${IOT_POLICY_NAME}"
        local_version_id=""
        if [[ "${DRY_RUN}" != "true" ]]; then
            local_version_id="$(aws iot create-policy-version \
                --policy-name "${IOT_POLICY_NAME}" \
                --policy-document "file://${WORK_DIR}/iot-policy.json" \
                --set-as-default \
                --region "${REGION}" --output json | jq -r '.policyVersionId')"
            log "  New default version: ${local_version_id}"
        else
            echo "${C_CYAN}[dry-run]${C_RESET} aws iot create-policy-version --policy-name ${IOT_POLICY_NAME} --set-as-default ..."
        fi
        record "IoT Policy" "updated"
    else
        log "  Exists (document identical)"
        record "IoT Policy" "exists"
    fi
else
    log "  Creating..."
    run_or_dry aws iot create-policy \
        --policy-name "${IOT_POLICY_NAME}" \
        --policy-document "file://${WORK_DIR}/iot-policy.json" \
        --region "${REGION}" --output json >/dev/null
    record "IoT Policy" "created"
fi

# ==========================================================================
# (f) IoT Thing Groups (per-architecture)
# ==========================================================================
for group in "${THING_GROUPS[@]}"; do
    log "--- IoT Thing Group: ${group} ---"
    if aws_check_iot_thing_group_exists "${group}" >/dev/null; then
        log "  Exists"
        record "Thing Group (${group})" "exists"
    else
        log "  Creating..."
        run_or_dry aws iot create-thing-group \
            --thing-group-name "${group}" \
            --region "${REGION}" --output json >/dev/null
        record "Thing Group (${group})" "created"
    fi
done

# ==========================================================================
# (g) S3 Bucket
# ==========================================================================
log "--- S3 Bucket: ${S3_BUCKET} ---"
if aws_check_s3_bucket_exists "${S3_BUCKET}"; then
    log "  Exists"
    record "S3 Bucket" "exists"
else
    log "  Creating..."
    if [[ "${REGION}" == "us-east-1" ]]; then
        run_or_dry aws s3api create-bucket \
            --bucket "${S3_BUCKET}" \
            --region "${REGION}" --output json >/dev/null
    else
        run_or_dry aws s3api create-bucket \
            --bucket "${S3_BUCKET}" \
            --region "${REGION}" \
            --create-bucket-configuration "LocationConstraint=${REGION}" \
            --output json >/dev/null
    fi
    run_or_dry aws s3api put-bucket-versioning \
        --bucket "${S3_BUCKET}" \
        --versioning-configuration Status=Enabled \
        --region "${REGION}"
    run_or_dry aws s3api put-public-access-block \
        --bucket "${S3_BUCKET}" \
        --public-access-block-configuration "BlockPublicAcls=true,IgnorePublicAcls=true,BlockPublicPolicy=true,RestrictPublicBuckets=true" \
        --region "${REGION}"
    record "S3 Bucket" "created"
fi

# ==========================================================================
# (h) S3 placeholder prefix
# ==========================================================================
log "--- S3 Prefix: benchmark-components/ ---"
if [[ "${DRY_RUN}" != "true" ]]; then
    aws s3api put-object \
        --bucket "${S3_BUCKET}" \
        --key "benchmark-components/.keep" \
        --body /dev/null \
        --region "${REGION}" --output json >/dev/null 2>&1 || true
fi
record "S3 Prefix" "exists"

# ==========================================================================
# Summary
# ==========================================================================
echo ""
log "=== Provisioning Summary ==="
printf "  ${C_CYAN}%-30s${C_RESET} %s\n" "Resource" "Action"
printf "  %-30s %s\n" "------------------------------" "-------"
for key in "IAM Role" "IAM Policy (S3)" "IAM Policy (Greengrass)" "IoT Role Alias" \
           "IoT Policy" "Thing Group (gg-benchmark-x86_64)" "Thing Group (gg-benchmark-aarch64)" \
           "Thing Group (gg-benchmark-armv7l)" "S3 Bucket" "S3 Prefix"; do
    printf "  %-30s %s\n" "${key}" "${SUMMARY[${key}]:-skipped}"
done
echo ""
log "Done. Account ${ACCOUNT_ID} in ${REGION} is ready for benchmark runs."
