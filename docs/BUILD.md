# Installing AWS IoT Greengrass nucleus lite from source

This document details building and installing Greengrass from source.

At the current time, Greengrass nucleus lite supports Linux targets using
systemd as the init system.

## Build tools

To build the project, you will need the following build dependencies:

- GCC or Clang
- CMake (at least version 3.22)
- Make or Ninja
- pkg-config
- git
- curl
- libssl-dev
- libcurl4-openssl-dev
- uuid-dev
- libzip-dev
- libsqlite3-dev
- libyaml-dev
- libsystemd-dev
- libevent-dev
- liburiparser-dev
- cgroup-tools

On Ubuntu, these can be installed with:

```sh
sudo apt update && sudo apt install build-essential pkg-config cmake git curl libssl-dev \
  libcurl4-openssl-dev uuid-dev libzip-dev libsqlite3-dev libyaml-dev \
  libsystemd-dev libevent-dev liburiparser-dev cgroup-tools
```

If your system's cmake is too old, you may need to build or download a newer
version. The provided `bootstrap-cmake.sh` script downloads a new cmake version
into `./build/cmake` and configures the project using that CMake with `./build`
as the build directory. If you do so, you can skip the configuring step below.

## Users/Groups

You will need to create the user/group for running the Greengrass nucleus lite
services, as well as the user/group for running components by default.

#### Nucleus service user/group

The nucleus service user/group is used by the nucleus systemd services and is
`ggcore:ggcore` by default. It can be configured using CMake as described later
in this document.

The rootDir must also be owned by the service user/group.

To create the service user/group:

For systems with groupadd/useradd:

```sh
groupadd ggcore
useradd -Ng ggcore ggcore
```

For systems with addgroup/adduser:

```sh
addgroup ggcore
adduser -g <gid from previous command> ggcore
```

Now before we continue any further we need to get the device credentials. You
may want to get certificate for the device using
[Provisioning guide](Provisioning.md).

Then run:

```
sudo mkdir /etc/greengrass/ggcredentials
//cp your aws credentials(device certificates, private key, root ca) to this folder
chown -R ggcore:ggcore /etc/greengrass/ggcredentials

mkdir /var/lib/greengrass
chown ggcore:ggcore /var/lib/greengrass
```

When [building](#building), provide this user and group via the
[configuration flags](#configuration-flags) `GGL_SYSTEMD_SYSTEM_USER` and
`GGL_SYSTEMD_SYSTEM_GROUP`.

#### Component user/group

The default user/group for components is set in your Greengrass configuration.
See the [posixUser in the setup guide](SETUP.md#configuring-greengrass) for more
info. Greengrass will configure components without an explicit user/group to run
as that user/group. This is listed in the sample configuration as
`gg_component:gg_component`.

Run the following with `gg_component`/`gg_component` substituted with your
user/group.

For systems with groupadd/useradd:

```sh
groupadd gg_component
useradd -Ng gg_component gg_component
```

For systems with addgroup/adduser:

```sh
addgroup gg_component
adduser -g <gid from previous command> gg_component
```

## (Optional) Using Podman

Instead of installing Greengrass Lite directly on a system, you can use a
container. The provided container has the build dependencies, system users
already provided, as well as Greengrass Lite built and installed.

The following steps assume you want to use `./run` for persistent state, you
place your Greengrass config file in `./run/config.yaml`, and certs/keys in
`./run/certs`. See [Setup guide](SETUP.md) for info on required keys. The keys
in [the built-in config](../misc/container/01defaults.yaml) are already set for
you and don't need to be in your config file.

A sample config file for the container is below:

```yml
system:
  privateKeyPath: "/var/lib/greengrass/certs/device.key"
  certificateFilePath: "/var/lib/greengrass/certs/device.pem"
  rootCaPath: "/var/lib/greengrass/certs/AmazonRootCA1.pem"
  thingName: "ExampleGreengrassCore"
services:
  aws.greengrass.NucleusLite:
    configuration:
      awsRegion: "<aws-region>"
      iotCredEndpoint: "<your-endpoint>.credentials.iot.<aws-region>.amazonaws.com"
      iotDataEndpoint: "<your-endpoint>-ats.iot.<aws-region>.amazonaws.com"
      iotRoleAlias: "GreengrassCoreTokenExchangeRoleAlias"
```

Docker does not fully support running systemd containers, however you can use
podman. These steps allow you to enter a pre-configured container:

```sh
sudo apt install podman
podman build . -t ggl:latest
podman run -it -v $PWD/run/config.yaml:/etc/greengrass/config.yaml \
  -v $PWD/run/certs:/var/lib/greengrass/certs \
  --replace --name ggl ggl:latest
```

To persist the Greengrass Lite run dir, you can bind a host directory to
`/var/lib/greengrass` (assuming certs/keys are in `./run/rootPath/certs`):

```sh
podman run -it -v $PWD/run/config.yaml:/etc/greengrass/config.yaml \
  -v $PWD/run/rootPath:/var/lib/greengrass \
  --replace --name ggl ggl:latest
```

You may need to run `chown -R ggcore:ggcore /var/lib/greengrass/certs`.

Note to not bind over the entirety of `/etc/greengrass` (this will hide the
default config fragment in the container).

## Building

The project is configured with CMake in the standard way.

The following examples assume you are in the unpacked source directory, and you
are using `./build` as the build directory.

To make a release build configured for minimal size, run:

```sh
cmake -B build -D CMAKE_BUILD_TYPE=MinSizeRel
```

To set the install directory to a location other than the standard system
locations, set `CMAKE_INSTALL_PREFIX`.

For example:

```sh
cmake -B build -D CMAKE_INSTALL_PREFIX=/usr/local
```

To build then run `make`:

```sh
make -C build -j$(nproc)
```

To install to the system, run:

```sh
make -C build install
```

To uninstall from the system, run:

```sh
make -C build uninstall
```

## Configuration flags

The following configuration flags may be set with cmake (with `-D`):

- `GGL_LOG_LEVEL`

  This can be set to `NONE`, `ERROR`, `WARN`, `INFO`, `DEBUG`, or `TRACE` to set
  the logging level.

- `GGL_SYSTEMD_SYSTEM_USER`

  The system user to use for Greengrass Lite nucleus services (not components).
  Must exist on system.

- `GGL_SYSTEMD_SYSTEM_GROUP`

  The system group to use for Greengrass Lite nucleus services (not components).
  Must exist on system.

- `GGL_SYSTEMD_SYSTEM_DIR`

  The directory to install systemd service files into. Should be set to
  `/lib/systemd/system` or `/etc/systemd/system` unless you are building it for
  a package.
