# Fleet provisioning

Fleet provisioning is an alternative way of provisioning a device with claim
certificates. This allows you to provision a fleet of devices with a common
claim certificate.

To get started, you need AWS claim certificates from IoT Core to obtain valid
certificates. You can follow the link
[here](https://docs.aws.amazon.com/greengrass/v2/developerguide/fleet-provisioning-setup.html)
to learn how to create appropriate policies and claim certificates.

Greengrass nucleus lite's fleet provisioning generates CSR and private keys
locally and then sends the CSR to IoT Core to generate a certificate. This
behavior is different from the default behavior of Greengrass classic's fleet
provisioning. Therefore, make sure your claim certificate has connect, publish,
subscribe, and receive access to `CreateCertificateFromCsr` and `RegisterThing`
topics mentioned in the
[linked AWS docs](https://docs.aws.amazon.com/iot/latest/developerguide/fleet-provision-api.html).

> Note in this demo, we are using restrictive production policies, please double
> check the policies accordingly.

We also support TPM with fleet provisioning. You can store both the claim
private key and the permanent private key in the TPM to generate certificates.
Detailed instructions are shown below.

## Before getting started

Before running fleet provisioning manually, you need to consider a few important
steps:

1. This section assumes that the system has already met the dependencies
   mentioned in [SETUP.md](../SETUP.md#dependencies).
2. Make sure you are logged in as root.
3. Make sure you do not fill `iotCredEndpoint/iotDataEndpoint` under
   `aws.greengrass.NucleusLite`. You should only fill these fields under
   `aws.greengrass.fleet_provisioning`'s config. See the
   [sample config below](#configyaml).
4. If this is not your first run, remove the socket at
   `/run/greengrass/iotcoredfleet`, if it exists.
5. If you want to enable TPM support, follow the
   [TPM Setup step](../TPM_SUPPORT.md#tpm-setup).

## Setting up the cloud side for provisioning

The first step to fleet provisioning is to set up the cloud infrastructure so
that all devices follow the same process of generating certificates and things.

The CloudFormation template,
[fleet-provisioning-cfn.yaml](./fleet-provisioning-cfn.yaml) provides a
maintainable way of bringing up cloud resources.

Now export access credentials to your account. Below is an example of exporting
access keys with environment variables. For the perpose of demo, I am using
admin access keys. You may use other AWS-provided services to give the CLI
access to your account:

```
export AWS_ACCESS_KEY_ID=[REPLACE HERE]
export AWS_SECRET_ACCESS_KEY=[REPLACE_HERE]
export AWS_DEFAULT_REGION=[REPLACE_HERE]
```

Make sure that the [generate_claim.sh](./generate_claim.sh) shell script has
execute permissions and then run the script.

```
chmod +x ./generate_claim.sh
./generate_claim.sh
```

Optionally, you can specify a custom CSR common name:

```
CSR_COMMON_NAME="my-custom-name" ./generate_claim.sh
```

If not specified, the default value "aws-greengrass-nucleus-lite" will be used.

However, if you want to enable TPM support, make sure the
[generate_claim_tpm.sh](./generate_claim_tpm.sh) script has execute permissions,
and then run it instead.

```
chmod +x ./generate_claim_tpm.sh
./generate_claim_tpm.sh
```

Once the stack is up and running, you should see the following resources in the
cloud:

- CloudFormation stack called `GreengrassFleetProvisioning` (or
  `GreengrassFleetProvisioning-TPM`)
  - IoT policies
  - IAM policies
  - Role and RoleAlias
  - Thing and ThingGroup
  - Lambda function called `MacValidationLambda`
- Claim certificates under your build directory and cloud
  - Verify the printed certificate-id with the one in the cloud at IoT Core >
    Security > Certificates
- A partial config file `part.config.yaml` on disk at
  `${PROJECT_ROOT}/fleetprovisioning`. This is an incomplete config file and is
  only provided for ease of copying

Once you see all the resources in the cloud, you can continue to the next steps.

> Note: While deleting the CloudFormation stack, make sure that any related IoT
> policies do not have a certificate attached, as that will prevent it from
> auto-deleting.

## Setting up the device side for provisioning

Here, the template name is `GGFleetProvisioningTemplate` (or
`GGFleetProvisioningTemplate-TPM`) and the template requires (based on the above
example) you to provide only a MAC address as the serial number in the template
parameter. Your nucleus config should roughly look as below:

### `config.yaml`

```yaml
---
system:
  privateKeyPath: "" #[Must leave blank]
  certificateFilePath: "" #[Must leave blank]
  rootCaPath: "" #[Must leave blank]
  rootPath: "/var/lib/greengrass/" #[Modify if needed]
  thingName: "" #[Must leave blank]
services:
  aws.greengrass.NucleusLite:
    componentType: "NUCLEUS"
    configuration:
      awsRegion: "us-east-1" #[Modify if needed]
      iotCredEndpoint: "" #[Must leave blank]
      iotDataEndpoint: "" #[Must leave blank]
      iotRoleAlias: "GreengrassV2TokenExchangeRoleAlias-GreengrassFleetProvisioning" #[Modify if needed]
      runWithDefault:
        posixUser: "ggcore:ggcore" #[Modify if needed]
      greengrassDataPlanePort: "8443"
  aws.greengrass.fleet_provisioning:
    configuration:
      iotDataEndpoint: "aaaaaaaaaaaaaa-ats.iot.us-east-1.amazonaws.com" #[Modify here]
      iotCredEndpoint: "cccccccccccccc.credentials.iot.us-east-1.amazonaws.com" #[Modify here]
      rootCaPath: "/path/to/AmazonRootCA1.pem" #[Modify here]
      claimKeyPath: "path/to/private.pem.key" #[Modify here]
      claimCertPath: "path/to/certificate.pem.crt" #[Modify here]
      csrCommonName: "aws-greengrass-nucleus-lite" #[Modify here]
      templateName: "GreengrassFleetProvisioningTemplate" #[Modify here]
      templateParams:
        SerialNumber: "a2_b9_d2_5a_fd_f9" #[Modify here]
      # Optional: Custom paths for generated certificates (defaults to /var/lib/greengrass/credentials/)
      # csrPath: "/custom/path/cert_req.pem" #[Optional]
      # certPath: "/custom/path/certificate.pem" #[Optional]
      # keyPath: "/custom/path/priv_key" #[Optional]
```

Things to note about the above config:

1. You can copy and paste from the generated sample file `part.config.yaml`. The
   starting point is `aws.greengrass.fleet_provisioning` through `templateName`.
   Note that `templateParams` is still required.
2. **Optional certificate paths**: You can specify custom paths for the
   generated certificates using `csrPath`, `certPath`, and `keyPath`. If not
   specified, files will be created in `/var/lib/greengrass/credentials/` by
   default.
   - `csrPath`: Path where the Certificate Signing Request (CSR) will be
     generated
   - `certPath`: Path where the device certificate will be stored
   - `keyPath`: Path where the private key will be stored
   - These paths can be used to customize both the location and filename of the
     generated certificates (e.g., `/custom/dir/my-device-cert.pem`)
   - Note: The CSR file is automatically removed after successful provisioning
   - If custom paths are provided, only those specific files will be created at
     the custom locations; other files will still use the default directory
   - Note: we currently do not support the custom persistent handle for the
     optional key path.
3. The system configuration paths (`privateKeyPath`, `certificateFilePath`,
   etc.) will be automatically updated after successful provisioning to point to
   the generated certificate locations.
4. If you enable TPM support, modify the `claimKeyPath` with a persistent
   handle, e.g. `"handle:0x81000000"`

Once you have finished editing the `config.yaml` file with your fleet
provisioning settings, deploy it to the system and start the Greengrass
services. Run the following commands, assuming your current working directory is
the root of the greengrass repository:

```sh
mkdir -p /etc/greengrass
mkdir -p /var/lib/greengrass/credentials/
cp ./config.yaml /etc/greengrass/config.yaml

sudo rm -rf /var/lib/greengrass/config.db
sudo systemctl stop greengrass-lite.target
sudo systemctl start greengrass-lite.target
```

Wait for a few seconds and then in a shell, run the fleet provisioning binary
with the following command:

```sh
$ sudo /usr/local/bin/fleet-provisioning
```

If you cannot find `fleet-provisioning` under `/usr/local/bin`, then reconfigure
CMake with the flag `-D CMAKE_INSTALL_PREFIX=/usr/local`, rebuild, and
reinstall.

If you enable TPM support, reconfigure CMake with the flag `-D TPM_SUPPORT=ON`,
rebuild, and then rerun the Greengrass Lite. After that, run the
fleet-provisioning binary with the following flag:

```sh
$ sudo /usr/local/bin/fleet-provisioning --use-tpm
```

This will trigger the fleet provisioning script, which will take a few minutes
to complete.

If you are storing the standard output, look for the log:
`Process Complete, Your device is now provisioned`.

Once successfully provisioned please restart the greengrass service with

```
sudo systemctl restart greengrass-lite.target
```

this will allow greengrass services to load the new config changes. And only
then will the core device be visible in the console.

> You might see some debug logs such as
> `process is getting terminated by signal 15`. This is expected and correct
> behavior.
