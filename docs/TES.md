# TES Setup Doc

Using TES requires setting up a few policies and roles in the cloud first.

#### Create the IAM Role

- Login to your aws account and go to IAM
- Within IAM click `Roles` from the left panel in the webpage
- Click `Create role` that's present on the top right page
- Select custom trust policy and replace the json with the following value

```
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Principal": {
                "Service": [
                    "credentials.iot.amazonaws.com"
                ]
            },
            "Action": "sts:AssumeRole"
        }
    ]
}
```

The above trust relationship is to allow someone to assume a role and vend
credentials when requested.

- Click `Next`
- On the `Add permissions` page we may attach appropriate policy according to
  your need. Here you may want to decide what services and the action allowed on
  that services one can perform with the credentials once presented
- Now fill in the name and a short description for the role. For GG demo we use
  `GreengrassV2TokenExchangeRole`. Note down the role name used.
- Click `create`

#### Create the IoT Core Role Alias

- Now go the search bar and look for `IoT core`
- On the left panel menu look for `Security` expand and then `Role aliases`
- Click on `Create role alias`
- Provide a appropriate name, for demo `GreengrassV2TokenExchangeRoleAlias`
- Select the role `GreengrassV2TokenExchangeRole`[or the name you gave] in the
  Role section.
- Select the appropriate time you would like a given credential to be valid for
  and then hit `Create`
- Find the newly created RoleAlias and note down the `Role ARN`

#### Create the IoT Core Policy

- On the left panel menu look for `Security` expand and then `Policy`
- Hit `Create Policy`
- Pick an appropriate policy name, for demo
  `GreengrassV2TokenExchangeRoleAttachPolicy`
- Policy effect: `Allow`, Policy action: `iot:AssumeRoleWithCertificate`, Policy
  resource: `[the role arn previously copied]`
- Policy effect: `Allow`, Policy action: `iot:Connect`, Policy resource `*`
- Policy effect: `Allow`, Policy action: `iot:Publish`, Policy resource `*`
- Policy effect: `Allow`, Policy action: `iot:Subscribe`, Policy resource `*`
- Policy effect: `Allow`, Policy action: `iot:Receive`, Policy resource `*`
- Policy effect: `Allow`, Policy action: `greengrass:*`, Policy resource: `*`
  - The `greengrass:*` policy action may not auto-fill or be in the dropdown.
    You can add it via the JSON editor.
- Click on `Create`

#### Create the IoT Core Thing and get a device certificate

This thing will represent your device, and the certificate with authenticate the
device. If you already have a thing and certificate for your device you may skip
this section and
[attach your policy to your existing certificate](https://docs.aws.amazon.com/iot/latest/developerguide/attach-to-cert.html).
If you need to create a thing, proceed with this section.

- On the left panel expand `All devices` and select `Things`
- Click `Create things`, and `Create single thing`
- Give it a name e.g. `my-gglite-dev-local-testing-core`, click `Next`. Note
  down the thing name and which region you're creating it in.
- Click `Auto-generate a new certificate`, click `Next`
- Attach your IoT Core policy created earlier, and click `Create thing`
- Download all the certificates and keys presented and move them to your device.
  - Confirm that the Root CA files have a trailing newline in the file. If not,
    add the trailing newline.

#### Get the IoT Core endpoints

[Open up AWS CloudShell](https://docs.aws.amazon.com/cloudshell/latest/userguide/getting-started.html#launch-region-shell)
in the same region as your IoT Core Thing and run the following command:

`aws iot describe-endpoint --endpoint-type iot:CredentialProvider`

Note down the credentials endpoint address.

Then run `aws iot describe-endpoint --endpoint-type iot:Data-ats`

Note down the data endpoint address.

#### Next steps

Once these steps done, you can continue with the [setup guide](SETUP.md) and
input the information about the things you just created such as your role alias,
thing, certificate, private key, endpoints, etc. into your nucleus config.
