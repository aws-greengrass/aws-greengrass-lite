# Installing AWS Greengrass Nucleus Lite from source

This document details building and installing Greengrass from source.

At the current time, Greengrass Lite supports Linux targets using systemd as the
init system.

## Dependencies

This project uses third party library dependencies on Ubuntu > 24.04, these can be installed with:

```sh
sudo apt install libssl-dev libcurl4-openssl-dev libsqlite3-dev libyaml-dev \
  libsystemd-dev liburiparser-dev uuid-dev libevent-dev libzip-dev cgroup-tools
```

## Build tools

To build the project, you will need the following build dependencies:

- GCC or Clang
- CMake (at least version 3.25)
- Make or Ninja
- pkg-config
- git
- curl

On Ubuntu, these can be installed with:

```sh
sudo apt install build-essential pkg-config cmake git curl
```

If your system's cmake is too old, you may need to build or download a newer
version. The provided `bootstrap-cmake.sh` script downloads a new cmake version
into `./build/cmake` and configures the project using that CMake with `./build`
as the build directory. If you do so, you can skip the configuring step below.

## User/Group

You will need to create the user/group for running the Greengrass Lite nucleus
services, as well as the user/group for running components by default.

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

Then run:

```
sudo mkdir /ggcredentials
//cp your aws credentials(device certificates, private key, root ca) to this folder
chown -R ggcore:ggcore /ggcredentials

mkdir /var/lib/greengrass
chown ggcore:ggcore /var/lib/greengrass
```

The default user/group for components is set in your Greengrass configuration.
Greengrass will configure components without an explicit user/group to run as
that user/group. This is listed in the sample configuration as
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
container. The provided container has the build dependencies and system users
already provided.

Docker does not fully support running systemd containers, however you can use
podman. These steps allow you to enter a pre-configured container:

```sh
sudo apt install podman
podman build misc/container -t ggl:latest
podman run -it -v $PWD/..:/work --replace --name ggl ggl:latest
cd /work/aws-greengrass-lite/
```

Inside of the container, you can continue following the rest of the below steps.

To persist the Greengrass Lite runDir, you can bind a host directory to
`/var/lib/greengrass`:

```sh
podman run -it -v $PWD/..:/work -v $PWD/run:/var/lib/greengrass --replace --name ggl ggl:latest
```

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
