# AWS Greengrass Lite

AWS IoT Greengrass runtime for constrained devices.

## Supported Greengrass V2 IPC commands (Features)

| Feature                                 | Supported | Schedule | Plugin that provides support |
| :-------------------------------------- | :-------: | :------: | :--------------------------- |
| SubscribeToTopic                        |     x     |   now    | local_broker                 |
| PublishToTopic                          |     x     |   now    | local_broker                 |
| PublishToIoTCore                        |     x     |   now    | iot_broker                   |
| SubscribeToIoTCore                      |     x     |   now    | iot_broker                   |
| UpdateState                             |           |   soon   |                              |
| SubscribeToComponentUpdates             |           |   soon   |                              |
| DeferComponentUpdate                    |           |   soon   |                              |
| GetConfiguration                        |           |   soon   |                              |
| UpdateConfiguration                     |           |   soon   |                              |
| SubscribeToConfigurationUpdate          |           |   soon   |                              |
| SubscribeToValidateConfigurationUpdates |           |   soon   |                              |
| SendConfigurationValidityReport         |           |   soon   |                              |
| GetSecretValue                          |           |  future  |                              |
| PutComponentMetric                      |           |   soon   |                              |
| GetComponentDetails                     |           |  future  |                              |
| RestartComponent                        |           |  future  |                              |
| StopComponent                           |           |  future  |                              |
| CreateLocalDeployment                   |     x     |   now    | native_plugin                |
| CancelLocalDeployment                   |           |  future  |                              |
| GetLocalDeploymentStatus                |           |  future  |                              |
| ListLocalDeployments                    |           |  future  |                              |
| ValidateAuthorizationToken              |           |  future  |                              |
| CreateDebugPassword                     |           |  future  |                              |
| PauseComponent                          |           |  future  |                              |
| ResumeComponent                         |           |  future  |                              |
| GetThingShadow                          |           |  future  |                              |
| UpdateThingShadow                       |           |  future  |                              |
| DeleteThingShadow                       |           |  future  |                              |
| ListNamedShadowsForThing                |           |  future  |                              |
| SubscribeToCertificateUpdates           |           |  future  |                              |
| VerifyClientDeviceIdentity              |           |  future  |                              |
| GetClientDeviceAuthToken                |           |  future  |                              |
| AuthorizeClientDeviceAction             |           |  future  |                              |
