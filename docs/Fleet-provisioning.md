# Fleet provisioning

Fleet provisioning is an alternative way of provisioning a device with claim
certificates. This will allow you provision a fleet of devices with a common
claim certificate.

To get started you would need aws claim certificates from IoT Core, so that we
can get valid certificates. you can follow the link
[here](https://docs.aws.amazon.com/greengrass/v2/developerguide/fleet-provisioning-setup.html)
to learn how to create appropriate policies and claim certificate.

```
Note:
Currently, fleet provisioning can only be run manually.
Hence you will need to follow few important pre-steps

1. Make sure you are logged in as root
2. Allow read access to all user for your certificates
    chmod -R +rx /ggcredentials/
3. Make sure you do not fill iotCredEndpoint/iotDataEndpoint under
  `aws.greengrass.NucleusLite` you should only fill these fields
  under `aws.greengrass.fleet_provisioning`'s config
4. If this is your not first run, remove the socket at
    /run/greengrass/iotcoredfleet, if it exists
```

Sample Fleet provisioning template:

```json
{
  "Parameters": {
    "SerialNumber": {
      "Type": "String"
    },
    "AWS::IoT::Certificate::Id": {
      "Type": "String"
    }
  },
  "Resources": {
    "policy_TestAllowAllPolicy": {
      "Type": "AWS::IoT::Policy",
      "Properties": {
        "PolicyName": "TestPolicy"
      }
    },
    "certificate": {
      "Type": "AWS::IoT::Certificate",
      "Properties": {
        "CertificateId": {
          "Ref": "AWS::IoT::Certificate::Id"
        },
        "Status": "Active"
      }
    },
    "thing": {
      "Type": "AWS::IoT::Thing",
      "OverrideSettings": {
        "AttributePayload": "MERGE",
        "ThingGroups": "DO_NOTHING",
        "ThingTypeName": "REPLACE"
      },
      "Properties": {
        "AttributePayload": {},
        "ThingGroups": [],
        "ThingName": {
          "Fn::Join": [
            "",
            [
              "greengrass",
              {
                "Ref": "SerialNumber"
              }
            ]
          ]
        }
      }
    }
  }
}
```

Here we can assume your template name is `FleetTestNew` and your template
requires you to only provide a serial number as parameter. Then your nucleus
config should roughly look as below.

```yaml
---
system:
  privateKeyPath: ""
  certificateFilePath: ""
  rootCaPath: "/ggcredentials/fleetClaim/AmazonRootCA1.pem" #[Modify here]
  rootPath: "/var/lib/greengrass/" #[Modify here]
  thingName: "" #[Must leave blank]
services:
  aws.greengrass.NucleusLite:
    componentType: "NUCLEUS"
    configuration:
      awsRegion: "us-east-1"
      iotCredEndpoint: "" #[Must leave blank]
      iotDataEndpoint: "" #[Must leave blank]
      iotRoleAlias: "GreengrassV2TokenExchangeRoleAlias"
      runWithDefault:
        posixUser: "user:group" #[Modify here]
      greengrassDataPlanePort: "8443"
  aws.greengrass.fleet_provisioning:
    configuration:
      iotDataEndpoint: "aaaaaaaaaaaaaa-ats.iot.us-east-1.amazonaws.com" #[Modify here]
      iotCredEndpoint: "cccccccccccccc.credentials.iot.us-east-1.amazonaws.com" #[Modify here]
      claimKeyPath: "/ggcredentials/fleetClaim/private.pem.key" #[Modify here]
      claimCertPath: "/ggcredentials/fleetClaim/certificate.pem.crt" #[Modify here]
      templateName: "FleetTestNew" #[Modify here]
      templateParams: '{"SerialNumber": "AAA55555"}' #[Modify here]
```

In root user shell, run fleet provisioning

```sh
cd ./run
../build/bin/fleet-provisioning
```

Now this will trigger the fleet provisioning script which will take a few
minutes to complete.

> Note: Device will reboot in case of successful run

If you are storing the standard output then look for log:
`Process Complete, Your device is now provisioned`.
