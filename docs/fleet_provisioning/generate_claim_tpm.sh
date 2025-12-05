#!/bin/bash
set -e

# Variables - modify these as needed
REGION="${AWS_DEFAULT_REGION:-us-west-2}"
ACCOUNT_ID=$(aws sts get-caller-identity --query "Account" --output text)
STACK_NAME="GreengrassFleetProvisioning-TPM"
TPM_KEY_HANDLE="${TPM_KEY_HANDLE:-0x81000000}" # Change to an available handle if this one is in use

# Calculate directories relative to script location
SCRIPT_DIR="$(pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../" && pwd)"
TEMP_DIR="${PROJECT_ROOT}/TPMFleetCerts"

# Check if fleet-provisioning-cfn.yaml exists
if [ ! -f "${SCRIPT_DIR}/fleet-provisioning-cfn.yaml" ]; then
    exit 1
fi

# Check if TPM tools are available
if ! command -v tpm2_createprimary &> /dev/null || ! command -v tpm2_create &> /dev/null; then
    echo "Error: TPM2 tools not found. Please install tpm2-tools package."
    exit 1
fi

# Check if TPM key handle is already in use
if tpm2_getcap handles-persistent | grep -q "${TPM_KEY_HANDLE}"; then
    echo "Warning: TPM key handle ${TPM_KEY_HANDLE} is already in use. Please change to the available key handle."
    exit 1
fi

# Create certificate directory
mkdir -p "${TEMP_DIR}"

echo "=== Setting up AWS IoT Fleet Provisioning for Greengrass with TPM ==="
echo "Region: ${REGION}"
echo "Account ID: ${ACCOUNT_ID}"
echo "Stack Name: ${STACK_NAME}"
echo "TPM Key Handle: ${TPM_KEY_HANDLE}"
echo "Temporary Directory: ${TEMP_DIR}"

# Deploy CloudFormation stack
echo -e "\n=== Deploying CloudFormation stack ==="
STACK_STATUS=$(aws cloudformation describe-stacks --stack-name ${STACK_NAME} --region "${REGION}" --query "Stacks[0].StackStatus" --output text 2>/dev/null || echo "DOES_NOT_EXIST")

if [ "$STACK_STATUS" == "ROLLBACK_COMPLETE" ] || [ "$STACK_STATUS" == "CREATE_FAILED" ] || [ "$STACK_STATUS" == "UPDATE_FAILED" ] || [ "$STACK_STATUS" == "UPDATE_ROLLBACK_COMPLETE" ]; then
  echo "Stack is in ${STACK_STATUS} state. Deleting it first..."
  aws cloudformation delete-stack --stack-name ${STACK_NAME} --region "${REGION}"
  echo "Waiting for stack deletion to complete..."
  aws cloudformation wait stack-delete-complete --stack-name ${STACK_NAME} --region "${REGION}"
  STACK_STATUS="DOES_NOT_EXIST"
fi

if [ "$STACK_STATUS" == "DOES_NOT_EXIST" ]; then
  echo "Creating new CloudFormation stack: ${STACK_NAME}"
  aws cloudformation create-stack \
    --stack-name ${STACK_NAME} \
    --template-body file://"${SCRIPT_DIR}"/fleet-provisioning-cfn.yaml \
    --parameters ParameterKey=ResourceSuffix,ParameterValue="-TPM" \
    --capabilities CAPABILITY_NAMED_IAM \
    --region "${REGION}"
  echo "Waiting for stack creation to complete..."
  aws cloudformation wait stack-create-complete --stack-name ${STACK_NAME} --region "${REGION}"
else
  echo "Updating existing CloudFormation stack: ${STACK_NAME}"
  aws cloudformation update-stack \
    --stack-name ${STACK_NAME} \
    --template-body file://"${SCRIPT_DIR}"/fleet-provisioning-cfn.yaml \
    --parameters ParameterKey=ResourceSuffix,ParameterValue="-TPM" \
    --capabilities CAPABILITY_NAMED_IAM \
    --region "${REGION}" || echo "No updates are to be performed."
fi

# Get outputs from CloudFormation stack
echo -e "\n=== Getting CloudFormation stack outputs ==="
PROVISIONING_TEMPLATE_NAME=$(aws cloudformation describe-stacks --stack-name ${STACK_NAME} --query "Stacks[0].Outputs[?OutputKey=='ProvisioningTemplateName'].OutputValue" --output text --region "${REGION}")
TOKEN_EXCHANGE_ROLE_ALIAS=$(aws cloudformation describe-stacks --stack-name ${STACK_NAME} --query "Stacks[0].Outputs[?OutputKey=='TokenExchangeRoleAlias'].OutputValue" --output text --region "${REGION}")
THING_GROUP_NAME=$(aws cloudformation describe-stacks --stack-name ${STACK_NAME} --query "Stacks[0].Outputs[?OutputKey=='ThingGroupName'].OutputValue" --output text --region "${REGION}")
MAC_VALIDATION_LAMBDA_ARN=$(aws cloudformation describe-stacks --stack-name ${STACK_NAME} --query "Stacks[0].Outputs[?OutputKey=='MacValidationLambdaArn'].OutputValue" --output text --region "${REGION}")

echo "Provisioning Template Name: ${PROVISIONING_TEMPLATE_NAME}"
echo "Token Exchange Role Alias: ${TOKEN_EXCHANGE_ROLE_ALIAS}"
echo "Thing Group Name: ${THING_GROUP_NAME}"
echo "MAC Validation Lambda ARN: ${MAC_VALIDATION_LAMBDA_ARN}"

# Generate TPM claim key and CSR
echo -e "\n=== Generating TPM claim key and CSR ==="
echo "Creating TPM primary key..."
tpm2_createprimary -C o -c "${TEMP_DIR}/primary.ctx"

echo "Creating ECC key..."
tpm2_create -C "${TEMP_DIR}/primary.ctx" -g sha256 -G ecc256 -r "${TEMP_DIR}/device.priv" -u "${TEMP_DIR}/device.pub"

echo "Loading the key..."
tpm2_load -C "${TEMP_DIR}/primary.ctx" -r "${TEMP_DIR}/device.priv" -u "${TEMP_DIR}/device.pub" -c "${TEMP_DIR}/device.ctx"

echo "Making the key persistent..."
tpm2_evictcontrol -C o -c "${TEMP_DIR}/device.ctx" "${TPM_KEY_HANDLE}"

echo "Generating CSR with TPM claim key..."
openssl req -new -provider tpm2 -key "handle:${TPM_KEY_HANDLE}" \
    -out "${TEMP_DIR}/claim.csr" \
    -subj "/CN=TPM_CSR"
echo "Successfully generated the CSR"

# Create certificate from CSR
echo -e "\n=== Creating claim certificate from CSR ==="
echo "Creating certificate from CSR..."
aws iot create-certificate-from-csr \
  --certificate-signing-request file://"${TEMP_DIR}/claim.csr" \
  --set-as-active \
  --region "${REGION}" > "${TEMP_DIR}"/cert-details.json
echo "Successfully created the certificate"

# Extract certificate from response and save to file
jq -r '.certificatePem' "${TEMP_DIR}"/cert-details.json > "${TEMP_DIR}/certificate.pem.crt"

# Attach the fleet provisioning policy to the claim certificate
echo "Attaching FleetProvisioningPolicy to certificate..."
CERT_ARN=$(jq -r '.certificateArn' "${TEMP_DIR}"/cert-details.json)
CERT_ID=$(jq -r '.certificateId' "${TEMP_DIR}"/cert-details.json)
aws iot attach-policy \
  --policy-name "FleetProvisioningPolicy-${STACK_NAME}" \
  --target "${CERT_ARN}" \
  --region "${REGION}"
echo "Successfully attached the policy to the certificate"

# Download the Amazon root CA certificate
echo -e "\n=== Downloading Amazon root CA certificate ==="
curl -s -o "${TEMP_DIR}"/AmazonRootCA1.pem https://www.amazontrust.com/repository/AmazonRootCA1.pem
echo "Successfully downloaded the Amazon root CA certificate"

# Get IoT endpoints
echo -e "\n=== Getting IoT endpoints ==="
IOT_DATA_ENDPOINT=$(aws iot describe-endpoint --endpoint-type iot:Data-ATS --region "${REGION}" --output text)
IOT_CRED_ENDPOINT=$(aws iot describe-endpoint --endpoint-type iot:CredentialProvider --region "${REGION}" --output text)

echo "IoT Data Endpoint: ${IOT_DATA_ENDPOINT}"
echo "IoT Credential Endpoint: ${IOT_CRED_ENDPOINT}"

# Create part.config.yaml snippet for TPM
echo -e "\n=== Creating part.config.yaml snippet ==="
{
  printf '# Fleet provisioning configuration with TPM\n'
  printf 'aws.greengrass.fleet_provisioning:\n'
  printf '\tconfiguration:\n'
  printf '\t\tiotDataEndpoint: "%s"\n' "${IOT_DATA_ENDPOINT}"
  printf '\t\tiotCredEndpoint: "%s"\n' "${IOT_CRED_ENDPOINT}"
  printf '\t\tclaimKeyPath: "handle:%s"\n' "${TPM_KEY_HANDLE}"
  printf '\t\tclaimCertPath: "%s"\n' "${TEMP_DIR}/certificate.pem.crt"
  printf '\t\trootCaPath: "%s"\n' "${TEMP_DIR}/AmazonRootCA1.pem"
  printf '\t\ttemplateName: "%s"\n' "${PROVISIONING_TEMPLATE_NAME}"
} > "${TEMP_DIR}"/part.config.yaml

echo -e "\n=== Fleet provisioning setup complete ==="
echo "Files generated in: ${TEMP_DIR}"
echo "  - part.config.yaml (TPM-enabled)"
echo "  - certificate.pem.crt"
echo "  - AmazonRootCA1.pem"
echo "  - claim.csr"
echo ""
echo "TPM Key Handle: ${TPM_KEY_HANDLE}"

# Display certificate ID if available
if [ -f "${TEMP_DIR}/cert-details.json" ]; then
  CERT_ID=$(jq -r '.certificateId' "${TEMP_DIR}"/cert-details.json)
  echo "Claim Certificate ID: ${CERT_ID}"
fi
