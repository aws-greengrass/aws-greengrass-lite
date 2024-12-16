# AWS Greengrass nucleus lite

AWS IoT Greengrass runtime for constrained devices.

The Greengrass nucleus lite provides a lightweight alternative to the Greengrass
nucleus runtime.

The nucleus lite aims to be compatible with the Greengrass nucleus, but
implements a subset of its functionality. Expect future releases to reduce the
feature gap.

## Getting started

See the [build guide](docs/INSTALL.md) for instructions to build and install
Greengrass Nucleus Lite from source.

To configure and run Greengrass Nucleus Lite, see the
[setup guide](docs/SETUP.md).

For setting up as a Greengrass developer, also see the
[developer setup guide](docs/DEVELOPMENT.md).

## Supported Greengrass V2 IPC commands (Features)

IPC support is provided by ggipcd. The support is translating the IPC command to
corebus. This table identifies the corebus component that does the work.

| Feature                                 | Daemon that provides support |
| :-------------------------------------- | :--------------------------- |
| SubscribeToTopic                        | ggpubsubd                    |
| PublishToTopic                          | ggpubsubd                    |
| PublishToIoTCore                        | iotcored                     |
| SubscribeToIoTCore                      | iotcored                     |
| UpdateState                             | _Future_                     |
| SubscribeToComponentUpdates             | _Future_                     |
| DeferComponentUpdate                    | _Future_                     |
| GetConfiguration                        | ggconfigd                    |
| UpdateConfiguration                     | ggconfigd                    |
| SubscribeToConfigurationUpdate          | ggconfigd                    |
| SubscribeToValidateConfigurationUpdates | _Future_                     |
| SendConfigurationValidityReport         | _Future_                     |
| GetSecretValue                          | _Future_                     |
| PutComponentMetric                      | _Future_                     |
| GetComponentDetails                     | _Future_                     |
| RestartComponent                        | _Future_                     |
| StopComponent                           | _Future_                     |
| CreateLocalDeployment                   | ggdeploymentd                |
| CancelLocalDeployment                   | _Future_                     |
| GetLocalDeploymentStatus                | _Future_                     |
| ListLocalDeployments                    | _Future_                     |
| ListComponents                          | _Future_                     |
| ValidateAuthorizationToken              | _Future_                     |
| CreateDebugPassword                     | _Future_                     |
| PauseComponent                          | _Future_                     |
| ResumeComponent                         | _Future_                     |
| GetThingShadow                          | _Future_                     |
| UpdateThingShadow                       | _Future_                     |
| DeleteThingShadow                       | _Future_                     |
| ListNamedShadowsForThing                | _Future_                     |
| SubscribeToCertificateUpdates           | _Future_                     |
| VerifyClientDeviceIdentity              | _Future_                     |
| GetClientDeviceAuthToken                | _Future_                     |
| AuthorizeClientDeviceAction             | _Future_                     |

## Additional Details

Known issues are documented [here](./docs/KNOWN_ISSUES.md) with some potential
workarounds. Additionally only basic recipe types are supported, more
information on missing features can be found
[here](./docs/RECIPE_SUPPORT_CHANGES.md).

## Security

See [CONTRIBUTING](docs/CONTRIBUTING.md#security-issue-notifications) for more
information.

## License

This project is licensed under the Apache-2.0 License.
