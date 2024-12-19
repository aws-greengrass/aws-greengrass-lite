# Fleet provisioning

Fleet provisioning is an alternative way of provisioning a device with claim
certificates. This will allow you provision a fleet of devices with a common
claim certificate.

To get started you would need aws claim certificates from IoT Core, so that we
can get valid certificates. you can follow the link
[here](https://docs.aws.amazon.com/greengrass/v2/developerguide/fleet-provisioning-setup.html)
to learn how to create appropriate policies and claim certificate.

Greengrass nucleus lite generates csr and private keys locally and then sends
the csr to iotcore to generate a certificate. This behavior is different from
Greengrass classic. Hence, make sure your claim certificate has connect,
publish, subscribe and receive access to `CreateCertificateFromCsr` and
`RegisterThing` topics mentioned in
[linked AWS docs](https://docs.aws.amazon.com/iot/latest/developerguide/fleet-provision-api.html).

## Before getting started:

Currently, fleet provisioning can only be run manually. Hence you will need to
follow few important pre-steps

1. This section assumes that the system has already met the dependencies
   mentioned in [SETUP.md](./SETUP.md#dependencies).
2. Make sure you are logged in as root.
3. Make sure you do not fill `iotCredEndpoint/iotDataEndpoint` under
   `aws.greengrass.NucleusLite` you should only fill these fields under
   `aws.greengrass.fleet_provisioning`'s config. See the
   [sample config below](#configyaml).
4. If this is your not first run, remove the socket at
   `/run/greengrass/iotcoredfleet`, if it exists.

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

## Setting up the device side for provisioning

Here we can assume your template name is `FleetTestNew` and your template
requires(based on above template) you to only provide a serial number as
parameter. Then your nucleus config should roughly look as below:

### `config.yaml`

```yaml
---
system:
  privateKeyPath: "" #[Must leave blank]
  certificateFilePath: "" #[Must leave blank]
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
      iotRoleAlias: "GreengrassV2TokenExchangeRoleAlias" #[Modify if needed]
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

Once completed, the config needs to be moved and all the services need to be
started (if not started already). Run the following command for it, assuming
your current working directory is root of greengrass repository:

```sh
$ mkdir -p /etc/greengrass
$ cp ./run/config.yaml /etc/greengrass/config.yaml
$ ./misc/run_nucleus
```

In root user shell, run the fleet provisioning binary.

If you changed `GGL_SYSTEMD_SYSTEM_USER` and `GGL_SYSTEMD_SYSTEM_GROUP`
mentioned in [CMakeLists.txt](../CMakeLists.txt), you can override default by
adding `-u "ggcore:ggcore"` at the end of following command:

```sh
$ ../build/bin/fleet-provisioning
```

Now this will trigger the fleet provisioning script which will take a few
minutes to complete.

> Note: Device will reboot in case of a successful run.

If you are storing the standard output then look for log:
`Process Complete, Your device is now provisioned`.

> You might see some error log such as `process is getting kill by signal 15`
> this is expected and correct behavior.
