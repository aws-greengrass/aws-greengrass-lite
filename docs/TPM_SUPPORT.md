# TPM 2.0 Support for AWS Greengrass Lite

This guide demonstrates how to set up AWS Greengrass Lite with TPM 2.0 support
using Amazon EC2 NitroTPM. The TPM provides hardware-backed security for device
identity and cryptographic operations.

## Step 1: Set up NitroTPM Instance

### 1.1 Launch Temporary Instance

Launch a temporary Ubuntu 24.04 instance on a NitroTPM-supported instance type
to create a custom AMI. This instance is used only for creating the custom AMI
and can be deleted after AMI creation.

1. Use Ubuntu 24.04 AMI on
   [NitroTPM-supported instance type](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/enable-nitrotpm-prerequisites.html#nitrotpm-instancetypes)
   (M5n, M5dn, R5n, R5dn, etc.)
2. From the storage section in the console, note:
   - Root device name (e.g., `/dev/sda1`)
   - Volume ID

### 1.2 Create EBS Snapshot

Create a snapshot of the root volume following the
[EBS snapshot creation guide](https://docs.aws.amazon.com/ebs/latest/userguide/ebs-create-snapshot.html).

Note the snapshot ID for the next step.

### 1.3 Register NitroTPM-enabled AMI

Create a custom AMI with TPM 2.0 support:

```bash
aws ec2 register-image \
    --name my-tpm-image \
    --boot-mode uefi \
    --architecture x86_64 \
    --root-device-name /dev/sda1 \
    --block-device-mappings DeviceName=/dev/sda1,Ebs={SnapshotId=snap-0abcdef1234567890} \
    --tpm-support v2.0
```

Replace `DeviceName` and `SnapshotId` with your actual values.

### 1.4 Launch Final Instance

Launch your production instance using the custom AMI created in the previous
step.

**Important**: When connecting via SSH, use the `ubuntu` user, not `root`.

### 1.5 Verify TPM Functionality

Verify that the TPM device is available:

```bash
ls -la /dev/tpm*
```

You should see `/dev/tpm0` and `/dev/tpmrm0` devices.

## Step 2: Install and Configure TPM Tools

### 2.1 Install Required Packages

```bash
sudo apt update
sudo apt install tpm2-openssl tpm2-tools tpm2-abrmd libtss2-tcti-tabrmd0
```

### 2.2 Verify TPM Device Permissions

Check that TPM devices have correct permissions:

```bash
ls -l /dev/tpm0    # Should be owned by tss:root with permissions 0660
ls -l /dev/tpmrm0  # Should be owned by tss:tss with permissions 0660
```

## Step 3: Configure OpenSSL TPM2 Provider

Edit the OpenSSL configuration file:

```bash
sudo vi /etc/ssl/openssl.cnf
```

Add the following configuration:

```ini
[openssl_init]
providers = provider_sect

[provider_sect]
default = default_sect
tpm2 = tpm2_sect

[default_sect]
activate = 1

[tpm2_sect]
identity = tpm2
module = /usr/local/lib64/tpm2.so
activate = 1
```

**Note**: Adjust the module path as necessary. You can find the correct path
using:

```bash
find /usr -name "tpm2.so"
```

## Step 4: Generate Persistent TPM Keys

### 4.1 Create Primary Key

```bash
sudo tpm2_createprimary -C o -c primary.ctx
```

### 4.2 Create ECC Key

```bash
sudo tpm2_create -C primary.ctx -g sha256 -G ecc256 -r device.priv -u device.pub
```

### 4.3 Load the Key

```bash
sudo tpm2_load -C primary.ctx -r device.priv -u device.pub -c device.ctx
```

### 4.4 Make Key Persistent

```bash
sudo tpm2_evictcontrol -C o -c device.ctx 0x81000002
```

This creates a persistent key with handle `0x81000002` (example handle).

## Step 5: Create Certificate Signing Request (CSR)

Generate a CSR using the persistent TPM key:

```bash
openssl req -new -provider tpm2 -key "handle:0x81000002" \
    -out device.csr \
    -subj "/CN=TPMThing"
```

Replace `0x81000002` with your chosen handle value and `TPMThing` with your
desired thing name.

## Step 6: Provision AWS IoT Thing

### 6.1 Create Thing in AWS IoT Console

1. Go to AWS IoT Console → Manage → Things
2. Create Thing with name `TPMThing` (or your chosen name)
3. Choose "Upload CSR" and upload `device.csr`
4. Download the thing's certificate as `device.pem`

For detailed manual provisioning steps, see the
[Provisioning Guide](Provisioning.md#manual-provisioning-by-creating-a-thing).

### 6.2 Download Amazon Root CA

```bash
curl -o AmazonRootCA1.pem https://www.amazontrust.com/repository/AmazonRootCA1.pem
```

## Step 7: Set up Greengrass Lite

### 7.1 Install Dependencies

```bash
sudo apt update && sudo apt install build-essential pkg-config cmake git curl libssl-dev \
  libcurl4-openssl-dev uuid-dev libzip-dev libsqlite3-dev libyaml-dev \
  libsystemd-dev libevent-dev liburiparser-dev cgroup-tools
```

### 7.2 Create Users and Groups

```bash
sudo groupadd ggcore
sudo useradd -Ng ggcore ggcore
sudo groupadd gg_component
sudo useradd -Ng gg_component gg_component
```

### 7.3 Configure User Permissions

Add the `ggcore` user to the `tss` group for TPM access:

```bash
sudo usermod -a -G tss ggcore
```

### 7.4 Set up Credentials Directory

```bash
sudo mkdir -p /etc/greengrass/ggcredentials
sudo cp device.pem AmazonRootCA1.pem /etc/greengrass/ggcredentials/
sudo chown -R ggcore:ggcore /etc/greengrass/ggcredentials
```

**Note**: Since we're using persistent TPM keys, no private key file needs to be
copied.

### 7.5 Create Greengrass Directory

```bash
sudo mkdir /var/lib/greengrass
sudo chown ggcore:ggcore /var/lib/greengrass
```

### 7.6 Configure Greengrass

Copy and modify the configuration file:

```bash
cp docs/examples/sample_nucleus_config.yaml ./config.yaml
```

Edit `config.yaml` with the following TPM-specific configuration:

```yaml
system:
  privateKeyPath: "handle:0x81000002" # Use your chosen handle
  certificateFilePath: "/etc/greengrass/ggcredentials/device.pem"
  rootCaPath: "/etc/greengrass/ggcredentials/AmazonRootCA1.pem"
  rootPath: "/var/lib/greengrass"
  thingName: "TPMThing"
  # Add your iotCredEndpoint, iotDataEndpoint, and iotRoleAlias
```

Copy the configuration to the system location:

```bash
sudo cp ./config.yaml /etc/greengrass/config.yaml
```

## Step 8: Build and Run Greengrass Lite

### 8.1 Build

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=MinSizeRel -DGGL_LOG_LEVEL=DEBUG
make -C build -j$(nproc)
sudo make -C build install
```

### 8.2 Run

```bash
sudo ./misc/run_nucleus
```

## Troubleshooting

### TPM Device Not Found

If `/dev/tpm0` is not present:

- Verify you're using a NitroTPM-supported instance type
- Ensure the AMI was created with `--tpm-support v2.0`
- Check that the instance was launched from the custom AMI

### Permission Denied Errors

If you encounter TPM access errors:

- Verify user is in the `tss` group: `groups $USER`
- Check TPM device permissions: `ls -l /dev/tpm*`
- Ensure you logged out and back in after adding to `tss` group

### OpenSSL Provider Issues

If TPM provider is not found:

- Verify `tpm2.so` path in `/etc/ssl/openssl.cnf`
- Check provider installation: `openssl list -providers`
- Ensure `tpm2-openssl` package is properly installed
