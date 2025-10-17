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

### In this demo, we are using a CloudFormation template with overly permissive policies. Please consider reducing the permissions in the template for production use as needed.

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

Once the stack is up and running, you should see the following resources in the
cloud:

- CloudFormation stack called `GreengrassFleetProvisioning`
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

Here, the template name is `GreengrassFleetProvisioningTemplate` and the
template requires (based on the above example) you to provide only a MAC address
as the serial number in the template parameter. Your nucleus config should
roughly look as follows:

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
      templateName: "GreengrassFleetProvisioningTemplate" #[Modify here]
      templateParams: '{"SerialNumber": "a2_b9_d2_5a_fd_f9"}' #[Modify here]
```

Things to note about the above config:

1. You can copy and paste from the generated sample file `part.config.yaml`. The
   starting point is `aws.greengrass.fleet_provisioning` through `templateName`.
   Note that `templateParams` is still required.
2. If you wish to move the certificate to a different location, then you need to
   update the path accordingly.
3. The value of `templateParams` must be in JSON format. Currently, only JSON
   format is supported.

Once completed, the config needs to be moved and all services need to be started
(if not started already). Run the following command, assuming your current
working directory is the root of the greengrass repository:

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

Here you can also add `--out_cert_path path/to/dir/` to provide an alternate
directory. The default is `/var/lib/greengrass/credentials/`.

This will trigger the fleet provisioning script, which will take a few minutes
to complete.

If you are storing the standard output, look for the log:
`Process Complete, Your device is now provisioned`.

> You might see some error logs such as
> `process is getting killed by signal 15`. This is expected and correct
> behavior.
