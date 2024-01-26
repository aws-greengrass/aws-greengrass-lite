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
| GetSecretValue                          |           |   soon   |                              |
| PutComponentMetric                      |           |   soon   |                              |
| GetComponentDetails                     |           |  future  |                              |
| RestartComponent                        |           |  future  |                              |
| StopComponent                           |           |  future  |                              |
| CreateLocalDeployment                   |     x     |   now    | native-plugin                |
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

First go to AWS IoT Core and create a thing.

Copy the sample config in `./setup/nucleus_config.yml`.

Configure the following in your config file

- privateKeyPath: Path to private key for the Thing
- certificateFilePath: Path to Thing certificate
- thingName: Name of the Thing
- rootpath: Absolute path to the run directory created above
- awsRegion: The AWS region with the Thing
- iotCredEndpoint: The IoT Core endpoint
- iotDataEndpoint: The IoT Core endpoint

### Running Greengrass Lite

For these examples greengrass is installed in the ~/gglite_testing folder. This
can be changed to suit your system. The settings shown will process the
config.yaml and populate the config folder with pre-processed versions. If you
change config.yaml you will need to delete the preprocessed files.

```bash
~/gglite_testing/bin/greengrass-lite -r ~/gglite_testing --init-config ~/gglite_testing/config/config.yaml
```

### Deployments

Follow the instructions
[here](https://docs.aws.amazon.com/greengrass/v2/developerguide/create-first-component.html)
to create a sample component.

#### CreateDeployment

To create a deployment use the
[Greengrass CLI](https://github.com/aws-greengrass/aws-greengrass-cli) component
and run

```bash
./greengrass-cli --ggcRootPath=~/gglite_testing deployment create --recipeDir /path/to/recipes --artifactDir /path/to/artifacts --merge "<component-name>=<version>"
```

### Compiling Greengrass Lite

```bash
cmake -B build
make -C build -j4
```

### Compiling Greengrass Lite for minimal footprint

The "-DUSE_OPENSSL" assumes openssl-devel installed on build machine and openssl
installed on target machine.

Build type "MinSizeRel" enables multiple size reduction options. Note that in
current build, shared plugins that link to DeviceSDK are still pulling in unused
code - this will be addressed.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=MinSizeRel
make -C build -j4
```

#### Installing in a user location

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=~/gglite_testing
make -C build -j4 install/strip
```

#### Installing in the system location

The default location on linux is /usr/bin

```bash
cmake -B build
make -C build -j4 install/strip
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
