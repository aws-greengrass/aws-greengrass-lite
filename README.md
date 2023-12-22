## Greengrass Lite

A smaller version of the Greengrass Nucleus for resource-constrained devices.

Greengrass Lite provides compatibility with Greengrass v2 components.
Additionally, it provides a new plugin interface; functionality can be built as
a shared library that uses the plugin interface to extend nucleus functionality.

See <https://github.com/aws-greengrass/aws-greengrass-nucleus/> for more.

The `nucleus` directory has the Greengrass Lite nucleus, which is the core of
the system.

The `plugins` directory has plugins, some of which implement optional Greengrass
Lite functionality, and others serve as examples of third-party plugins.

The `plugin_api` directory has the interface plugins should build against.

## Supported Greengrass V2 Features

| Feature                                 | Supported | Schedule | Plugin that provides support |
| :-------------------------------------- | :-------: | :------: | :--------------------------- |
| SubscribeToTopic                        |     x     |   now    | nucleus                      |
| PublishToTopic                          |     x     |   now    | nucleus                      |
| PublishToIoTCore                        |     x     |   now    | iot_broker                   |
| SubscribeToIoTCore                      |     x     |   now    | iot_broker                   |
| UpdateState                             |           |   soon   |                              |
| SubscribeToComponentUpdates             |           |   soon   |                              |
| DeferComponentUpdate                    |           |   soon   |                              |
| GetConfiguration                        |           |   soon   |                              |
| UdateConfiguration                      |           |   soon   |                              |
| SubscribeToConfigurationUpdate          |           |   soon   |                              |
| SubscribeToValidateConfigurationUpdates |           |   soon   |                              |
| SendConfigurationValidityReport         |           |   soon   |                              |
| GetSecretValue                          |           |   soon   |                              |
| PutComponentMetric                      |           |   soon   |                              |
| GetComponentDetails                     |           |  future  |                              |
| RestartComponent                        |           |  future  |                              |
| StopComponent                           |           |  future  |                              |
| CreateLocalDeployment                   |           |  future  |                              |
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

## Getting Started

> todo: This getting started section is in flux as the greengrass lite project
> matures.

### Installing Greengrass Lite binary

currently install is performed by the compile from source steps below.

### Configuring Greengrass Lite

A working installation will require a config file and a thing certificate
package.

Create First go to AWS IoT Core and create a thing. Put the c

### Running Greengrass Lite

For these examples greengrass is installed in the ~/gglite_testing folder. This
can be changed to suit your system. The settings shown will process the
config.yaml and populate the config folder with pre-processed versions. If you
change config.yaml you will need to either delete the preprocessed files or

```bash
~/gglite_testing/bin/greengrass-lite -r ~/gglite_testing --init-config ~/gglite_testing/config/config.yaml
```

### Compiling Greengrass Lite

```bash
cmake -B build
make -C build -j4
```

#### Installing in a user location

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=~/gglite_testing
make -C build -j4 install
```

#### Installing in the system location

The default location on linux is /usr/bin

```bash
cmake -B build
make -C build -j4 install
```

## Dependencies

The following dependencies are required to build Nucleus and all of the included
plugins

- Linux kernel >=5.15
- CMake >=3.22
- make >=4.2.1
- glibc >=2.31
- GCC >=9.4.0

## Security

See [CONTRIBUTING](CONTRIBUTING.md#security-issue-notifications) for more
information.

## License

This project is licensed under the Apache-2.0 License.
