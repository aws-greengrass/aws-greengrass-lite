# Fleet provisioning

Fleet provisioning is an alternative way of provisioning a device with claim
certificates. This will allow you provision a fleet of devices with a common
claim certificate.

To get started you would need aws claim certificates from IoT Core, so that we
can get valid certificates. you can follow the link
[here](https://docs.aws.amazon.com/greengrass/v2/developerguide/fleet-provisioning-setup.html)
to learn how to create appropriate policies and claim certificate.

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
  rootCaPath: "/home/ubuntu/repo/fleetClaim/AmazonRootCA1.pem"
  rootPath: "/home/ubuntu/aws-greengrass-lite/run_fleet/"
  thingName: ""
services:
  aws.greengrass.Nucleus-Lite:
    componentType: "NUCLEUS"
    configuration:
      awsRegion: "us-east-1"
      iotCredEndpoint: ""
      iotDataEndpoint: ""
      iotRoleAlias: "GreengrassV2TokenExchangeRoleAlias"
      runWithDefault:
        posixUser: "ubuntu:ubuntu"
      greengrassDataPlanePort: "8443"
      tesCredUrl: "http://127.0.0.1:8080/"
  aws.greengrass.fleet_provisioning:
    configuration:
      iotDataEndpoint: "dddddddddddddd-ats.iot.us-east-1.amazonaws.com"
      iotCredEndpoint: "aaaaaaaaaaaaaa.credentials.iot.us-east-1.amazonaws.com"
      claimKeyPath: "/home/ubuntu/fleetClaim/private.pem.key"
      claimCertPath: "/home/ubuntu/fleetClaim/certificate.pem.crt"
      templateName: "FleetTestNew"
      templateParams: '{"SerialNumber": "14ALES55UFA"}'
```

With all this setup for IoT core now let's begin provisioning the device. First
we will start an instance of ggconfigd

```sh
cd ./run
../build/bin/ggconfigd
```

In another shell, run the config script and the fleet provisioning

```sh
cd ./run
../build/bin/ggl-config-init --config ./init_config.yml
../build/bin/fleet-provisioning
```

Now this will trigger the fleet provisioning script which will take a few
minutes to complete, the shell doesn't automatically exits so look for a Info
level log: `Process Complete, Your device is now provisioned`. then you can kill
the process or wait for auto terminate of `300 seconds`.

You can then kill the config daemon as well.

Now you can return to `## Running the nucleus` step in [SETUP.md](SETUP.md)
