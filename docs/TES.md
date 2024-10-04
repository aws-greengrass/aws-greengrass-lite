# TES Setup Doc

Using TES requires setting up a few policies and roles in the cloud first.

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
                    "credentials.iot.amazonaws.com",
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
  `GreengrassV2TokenExchangeRole`
- Click `create`

- Now go the search bar and look for `IoT core`
- On the left panel menu look for `Security` expand and then `Role aliases`
- Click on `Create role alias`
- Provide a appropriate name, for demo `GreengrassV2TokenExchangeRoleAlias`
- Select the role `GreengrassV2TokenExchangeRole`[or the name you gave] in the
  Role section.
- Select the appropriate time you would like a given credential to be valid for
  and then hit `Create`
- Find the newly created RoleAlias and then copy the `Role ARN`

- On the left panel menu look for `Security` expand and then `Policy`
- Hit `Create Policy`
- Pick an appropriate policy name, for demo
  `GreengrassV2TokenExchangeRoleAttachPolicy`
- Policy effect: `Allow`, Policy action: `iot:AssumeRoleWithCertificate`, Policy
  resource: `[the role arn previously copied]`
- Click on `Create`

- Now all it is left is to attach this policy to wherever your device's
  certificate is. More can be found at
  `https://docs.aws.amazon.com/iot/latest/developerguide/attach-to-cert.html`

Congratulations, now the only thing left is to make sure the role alias
mentioned also matched with the GGLite's `nucleus_config.yml` as well as the
correct credential endpoint is set within file and GG device will be able to
vend TES credentials on demand.
