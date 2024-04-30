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

## Getting Started

> todo: This getting started section is in flux as the greengrass lite project
> matures.

## Dependencies

Building Nucleus and the included plugins was tested with:

- CMake: 3.25
- make: 4.2.1
- GCC: 9.4.0

The following runtime dependencies are needed:

- OpenSSL (tested with 3.0.13)
- Linux kernel (tested with 5.15)
- libc (tested with glibc 2.31)
- compiler support libraries (libgcc_s.so for GCC)

### Compiling Greengrass Lite

```bash
cmake -B build
make -C build -j4
```

For Linux, Greengrass Lite can be compiled with the Linux or Posix platforms.
The Linux platform uses modern Linux-specific features; for compatibility with
older systems, pass `-DGGLITE_PLATFORM=posix` to cmake.

### Compiling Greengrass Lite for minimal footprint

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

The default location on linux is directories under `/usr/`

```bash
cmake -B build
make -C build -j4 install/strip
```

### Configuring Greengrass Lite

A working installation will require a config file and a thing certificate
package.

First go to AWS IoT Core and create a thing.

Copy the sample config in `./samples/nucleus_config.yml`.

Configure the following in your config file

- privateKeyPath: Path to private key for the Thing
- certificateFilePath: Path to Thing certificate
- thingName: Name of the Thing
- rootpath: Absolute path to the run directory created above
- awsRegion: The AWS region with the Thing
- iotCredEndpoint: The IoT Core endpoint
- iotDataEndpoint: The IoT Core endpoint
- posixUser: colon separated user/group that generic components should run as

`posixUser` must be set to a valid user and group. If no colon and group is
provided, the user's default group is used. If not running Greengrass as root,
set this to the user Greengrass is running as.

### Running Greengrass Lite

For these examples, greengrass is installed in the `~/gglite_testing` folder.
This can be changed to suit your system. The settings shown will process the
config.yaml and populate the config folder with pre-processed versions. If you
change config.yaml you will need to delete the preprocessed files.

```bash
cd ~/gglite_testing
./bin/greengrass-lite -r . --init-config ./config/config.yaml
```

For developers trying to debug and contribute to the repository, you can use the
following command instead as it will allow you to enable debug symbols for a
trade off of a bigger footprint

```bash
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=./run
$ make -C build -j8 install
$ cd run
$ ./bin/greengrass-lite -r . --config ./config/config.yaml
```

#### Using bundled runtime dependencies

To have cmake bundle runtime dependencies, pass the
`-DINSTALL_RUNTIME_DEPENDENCIES=1` flag when configuring. Then the interpreter
will need to be patched:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX=~/gglite_testing -DINSTALL_RUNTIME_DEPENDENCIES=1
make -C build -j4 install/strip
cd ~/gglite_testing
# Match name for interpreter for your system
chmod +x lib/ld-linux-*
patchelf --set-interpreter $PWD/lib/ld-linux-* bin/* lib/* plugins/*
```

The `patchelf --set-interpreter` command is needed as CMake just copies the
dependencies into the install directory, and doesn't modify the ELF headers to
use the copied files. That command sets the ELF interpreter for all the
installables to the bundled `ld-linux-x86-64.so.2`.

When running, `LD_LIBRARY_PATH` must be set to the lib dir, such as in the
example below:

```bash
cd ~/gglite_testing
GG_ROOT=$PWD
LD_LIBRARY_PATH=$GG_ROOT/lib $GG_ROOT/bin/greengrass-lite -r . --init-config $GG_ROOT/config/config.yaml
```

### Deployments

Follow the instructions
[here](https://docs.aws.amazon.com/greengrass/v2/developerguide/create-first-component.html)
to create a sample component.

#### CreateLocalDeployment

To create a deployment, use the
[Greengrass CLI](https://github.com/aws-greengrass/aws-greengrass-cli) component
and run

```bash
./greengrass-cli --ggcRootPath=~/gglite_testing deployment create --recipeDir ./path/to/recipes --artifactDir ./path/to/artifacts --merge "<component-name>=<version>"
```

## Security

See [CONTRIBUTING](CONTRIBUTING.md#security-issue-notifications) for more
information.

## License

This project is licensed under the Apache-2.0 License.
