# AWS Greengrass Lite

AWS IoT Greengrass runtime for constrained devices.

## Supported Greengrass V2 IPC commands (Features)

| Feature                                 | Daemon that provides support |
| :-------------------------------------- | :--------------------------- |
| SubscribeToTopic                        | ggpubsubd                    |
| PublishToTopic                          | ggpubsubd                    |
| PublishToIoTCore                        | iotcored                     |
| SubscribeToIoTCore                      | iotcored                     |
| UpdateState                             |                              |
| SubscribeToComponentUpdates             |                              |
| DeferComponentUpdate                    |                              |
| GetConfiguration                        |                              |
| UpdateConfiguration                     | ggconfigd                    |
| SubscribeToConfigurationUpdate          |                              |
| SubscribeToValidateConfigurationUpdates | not planned                  |
| SendConfigurationValidityReport         | not planned                  |
| GetSecretValue                          | not planned                  |
| PutComponentMetric                      |                              |
| GetComponentDetails                     |                              |
| RestartComponent                        |                              |
| StopComponent                           |                              |
| CreateLocalDeployment                   |                              |
| CancelLocalDeployment                   |                              |
| GetLocalDeploymentStatus                |                              |
| ListLocalDeployments                    |                              |
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
