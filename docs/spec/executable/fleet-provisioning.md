# `fleet-provisioning` spec

`fleet-provisioning` will be a one time run executable that deals with all the
device provisioning requirements for the user. It will one time during the
installation phase and will no longer remain active for the rest of gg
operations

- [fleet-provisioning-1] The executable will use ggconfigd to pull in the
  provisioning information such as rootCA, pCertificate and any necessary
  details
- [fleet-provisioning-2] The executable will also be able to perform push the
  changes to the config's database such as thing name, private certificate and
  the device certificate
- [fleet-provisioning-3] The executable will be able to also start it's own
  instace of iotcored with claim certificate

## CLI parameters

## iotcored-path

- [fleet-provisioning-param-iotPath-1] The argument will allow the application
  to know the location of iotcored binary to start it's own instance
- [fleet-provisioning-param-iotPath-2] The iotPath argument can be provided by
  `--iotcored-path` or `-p`.
- [fleet-provisioning-param-iotPath-3] The iotPath argument is required.

### cert-file-path

- [fleet-provisioning-param-certfilePath-1] The argument will provide the path
  to the certificate that will be created locally as well as the onces that will
  be fetched from iot core. By deafult the location will be the current working
  directory
- [fleet-provisioning-param-certfilePath-2] The certfilePath argument can be
  provided by `--cert-file-Path` or `-c`.
- [fleet-provisioning-param-certfilePath-3] The certfilePath argument is
  optional.

## Environment Variables
