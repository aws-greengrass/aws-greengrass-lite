# AWS Greengrass Lite

🚧 🚧 🚧 WORK IN PROGRESS 🚧 🚧 🚧

> This software does not yet have an official release. Feel free to test and
> report issues you find, but some functionality may be buggy and there may be
> breaking changes before the initial release.

AWS IoT Greengrass runtime for constrained devices.

The Greengrass Lite nucleus provides a smaller alternative to the Classic
nucleus for Greengrass v2 deployments.

Greengrass Lite aims to maintain compatibility with the Classic nucleus, and
implements a subset of its functionality.

## Getting started

See the [build guide](docs/INSTALL.md) for instructions to build and install
Greengrass Lite from source.

To configure and run Greengrass Lite, see the [setup guide](docs/SETUP.md).

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
| UpdateState                             | - FUTURE -                   |
| SubscribeToComponentUpdates             | - FUTURE -                   |
| DeferComponentUpdate                    | - FUTURE -                   |
| GetConfiguration                        | ggconfigd                    |
| UpdateConfiguration                     | ggconfigd                    |
| SubscribeToConfigurationUpdate          | ggconfigd                    |
| SubscribeToValidateConfigurationUpdates | not planned                  |
| SendConfigurationValidityReport         | not planned                  |
| GetSecretValue                          | not planned                  |
| PutComponentMetric                      | - FUTURE -                   |
| GetComponentDetails                     | - FUTURE -                   |
| RestartComponent                        | - FUTURE -                   |
| StopComponent                           | - FUTURE -                   |
| CreateLocalDeployment                   | ggdeploymentd                |
| CancelLocalDeployment                   | - FUTURE -                   |
| GetLocalDeploymentStatus                | - FUTURE -                   |
| ListLocalDeployments                    | - FUTURE -                   |
| ListComponents                          | - FUTURE -                   |
| ValidateAuthorizationToken              | not planned                  |
| CreateDebugPassword                     | not planned                  |
| PauseComponent                          | not planned                  |
| ResumeComponent                         | not planned                  |
| GetThingShadow                          | not planned                  |
| UpdateThingShadow                       | not planned                  |
| DeleteThingShadow                       | not planned                  |
| ListNamedShadowsForThing                | not planned                  |
| SubscribeToCertificateUpdates           | not planned                  |
| VerifyClientDeviceIdentity              | not planned                  |
| GetClientDeviceAuthToken                | not planned                  |
| AuthorizeClientDeviceAction             | not planned                  |

## Security

See [CONTRIBUTING](docs/CONTRIBUTING.md#security-issue-notifications) for more
information.

## License

This project is licensed under the Apache-2.0 License.
