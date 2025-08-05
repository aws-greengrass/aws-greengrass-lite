# Provisioning a GG device

Here we discuss how to provision a Greengrass device and troubleshoot TES
issues, as it requires a detailed and precise setup to work as expected.

## GG device provisioning

Greengrass supports multiple ways to provision the device, such as Fleet
Provisioning, custom provisioning or manually downloading the device credentials
from AWS IoT Core console.

### Fleet Provisioning

You may refer to the
[Fleet Provisioning guide](./fleet_provisioning/fleet_provisioning.md) for
detailed instructions on how to provision your Greengrass device using Fleet
Provisioning.

### Custom Provisioning

Greengrass Lite is a collection of microservices, so it's not limited to a
specific provisioning solution. You may use your own provisioning method by
simply following a few principles:

- `ggconfig.d` will only look into `/etc/greengrass/config.yaml` or
  `/etc/greengrass/config.d/*.yaml` if it cannot find a database at
  `/var/lib/greengrass/config.db`
- The sole source of truth at any given point for greengrass configuration is
  `/var/lib/greengrass/config.db`
- If you modify/create the yaml file at `/etc/greengrass/config.yaml` before
  `ggconfigd` starts, then Greengrass will always initialize itself with the
  contents of the file. Hence, any systemd service file for provisioning should
  mark itself to run with `Before=ggl.core.ggconfigd.service` and should restart
  `greengrass-lite.target` at the end of provisioning to reload the changes to
  other microservices.

Any solution that respects the above rules will work with GG Nucleus Lite.

### Manual Provisioning

If you prefer to manually provision your Greengrass device, you can download the
device credentials from the console by following these steps:

- Go to:
  `IoT Core > Greengrass devices > Core devices > Set up core device > Set up one core device`
- Modify the name fields as required and then select `Nucleus Lite`
- Find the `Create thing` button and click it
- Then click `Download connection kit`

The downloaded connection kit not only has all the required certificates but
also creates the required cloud resources. Once downloaded, you can copy it to
the device and refer to it by updating the `config.yaml`.

NOTE: The created resources follow the least permissible access policy, so if a
resource has already been created in one region, it might conflict when a new
device is created in a different region. Double-check the information in
[Manual Provisioning by Creating a Thing](#manual-provisioning-by-creating-a-thing)
if you are having problems.

### Manual Provisioning by Creating a Thing

All the above steps perform the following process for you. This is a more
detailed explanation of the setup that other processes are doing under the hood.
You can also refer to the following steps as a troubleshooting method if you are
having problems with other steps.

#### Create the IAM Role

- Log in to your AWS account and go to IAM
- Within IAM, click `Roles` from the left panel
- Click `Create role` in the top right of the page
- Select custom trust policy and replace the JSON with the following value:

**GreengrassV2CoreDeviceRole**

```
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Principal": {
                "Service": [
                    "iot.amazonaws.com",
                    "credentials.iot.amazonaws.com"
                ]
            },
            "Action": "sts:AssumeRole",
            "Condition": {
                "StringEquals": {
                    "aws:SourceAccount": "754281915471"
                },
                "ArnLike": {
                    "aws:SourceArn": "arn:aws:iot:*:754281915471:rolealias/GreengrassV2TokenExchangeCoreDeviceRoleAlias"
                }
            }
        }
    ]
}
```

The above trust relationship is to allow
`GreengrassV2TokenExchangeCoreDeviceRoleAlias` to assume a role and vend
credentials when requested.

- Click `Next`
- On the `Add permissions` page, attach the appropriate policy according to your
  needs. Here you may want to decide what services and actions are allowed on
  those services that one can perform with the credentials once presented
- Fill in the name and a short description for the role. For the GG demo, we use
  `GreengrassV2CoreDeviceRole`. Note down the role name used.
- Click `Create`

> NOTE: If deploying components from S3, you may want to add
> `AmazonS3FullAccess` for development or a bucket-specific resource for
> production environments.

#### Create the IoT Core Role Alias

- Go to the search bar and look for `IoT Core`
- On the left panel menu, look for `Security`, expand it, and then select
  `Role aliases`
- Click on `Create role alias`
- Provide an appropriate name; for the demo, use
  `GreengrassV2CoreDeviceRoleAlias`
- Select the role `GreengrassV2CoreDeviceRole` (or the name you gave) in the
  Role section
- Select the appropriate time you would like a given credential to be valid for
  and then click `Create`
- Find the newly created RoleAlias and note down the `Role ARN`

#### Create the IoT Core Policy

- On the left panel menu, look for `Security`, expand it, and then select
  `Policies`
- Click `Create Policy`
- Pick an appropriate policy name; for the demo, use
  `GreengrassV2CoreDeviceRoleAttachPolicy`
- Policy effect: `Allow`, Policy action: `iot:AssumeRoleWithCertificate`, Policy
  resource: `[the role ARN previously copied]`
- Policy effect: `Allow`, Policy action: `iot:Connect`, Policy resource: `*`
- Policy effect: `Allow`, Policy action: `iot:Publish`, Policy resource: `*`
- Policy effect: `Allow`, Policy action: `iot:Subscribe`, Policy resource: `*`
- Policy effect: `Allow`, Policy action: `iot:Receive`, Policy resource: `*`
- Policy effect: `Allow`, Policy action: `greengrass:*`, Policy resource: `*`
  - The `greengrass:*` policy action may not auto-fill or be in the dropdown.
    You can add it via the JSON editor.
- Click on `Create`

> Note: This is only recommended for development environments. Please restrict
> resource access for production.

#### Create the IoT Core Thing and get a device certificate

This thing will represent your device, and the certificate will authenticate the
device. If you already have a thing and certificate for your device, you may
skip this section and
[attach your policy to your existing certificate](https://docs.aws.amazon.com/iot/latest/developerguide/attach-to-cert.html).
If you need to create a thing, proceed with this section.

- On the left panel, expand `All devices` and select `Things`
- Click `Create things`, then `Create single thing`
- Give it a name, e.g., `my-gglite-dev-local-testing-core`, then click `Next`.
  Note down the thing name and which region you're creating it in.
- Click `Auto-generate a new certificate`, click `Next`
- Attach your IoT Core policy created earlier, and click `Create thing`
- Download all the certificates and keys presented and move them to your device.
  - Confirm that the Root CA files have a trailing newline. If not, add the
    trailing newline.

#### Get the IoT Core endpoints

[Open up AWS CloudShell](https://docs.aws.amazon.com/cloudshell/latest/userguide/getting-started.html#launch-region-shell)
in the same region as your IoT Core Thing and run the following command:

`aws iot describe-endpoint --endpoint-type iot:CredentialProvider`

Note down the credentials endpoint address.

Then run `aws iot describe-endpoint --endpoint-type iot:Data-ats`

Note down the data endpoint address.

#### Next steps

Once these steps are done, you can continue with the [setup guide](SETUP.md) and
input the information about the things you just created (such as your role
alias, thing, certificate, private key, endpoints, etc.) into your nucleus
config.
