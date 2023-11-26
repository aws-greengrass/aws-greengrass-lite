## Fleet Provisioning Plugin

https://docs.aws.amazon.com/iot/latest/developerguide/provision-wo-cert.html

## Parameters

This plugin takes following parameters from config file

```yaml
---
services:
  aws.greengrass.FleetProvisioningByClaim:
    configuration:
      rootPath: "/"
      rootCaPath: "/path/to/AmazonRootCA1.pem"
      iotDataEndpoint: "device-data-prefix-ats.iot.us-west-2.amazonaws.com"
      claimKeyPath: "/path/to/claim.private.pem.key"
      claimCertPath: "/path/to/claim.pem.crt"
      deviceId: "device_id"
      templateName: "template_name"
      templateParams: '{"key1":"value1",...}'
      csrPath: "/path/to/claim.csr"
      mqttPort: 80
      proxyUrl: "http://my-proxy-server:1100"
      proxyUserName: "Mary_Major"
      proxyPassword: "pass@word1357"
```

### Required
- **rootPath**: Root path for Greengrass
- **rootCaPath**: Path of the root CA
- **iotDataEndpoint**: IoT data endpoint for the AWS account
- **templateName**: The provisioning template name
- **claimCertPath**: Path of the claim certificate on the device.
- **claimKeyPath**: Path of the claim certificate private key on the device

### Optional

- **csrPath**: CSR file to be used for creating the device certificate from a
  CSR
- **deviceId**: The device identifier which will be used as client id in the mqtt connection to AWS IoT
- **templateParams**: Json object of parameters which will be passed to
  provisioning template
- **proxyUrl**: Http proxy url to be used for mqtt connection. The url is of
  format _scheme://host:port_

  - scheme – The scheme, which must be http or https.
  - host – The host name or IP address of the proxy server.
  - port – (Optional) The port number. If you don't specify the port, then the
    Greengrass core device uses the following default values:
    - http – 80
    - https – 443

- **proxyUsername:** The username to use to authenticate to the proxy server.
- **proxyPassword:** The password to use to authenticate to the proxy server.
