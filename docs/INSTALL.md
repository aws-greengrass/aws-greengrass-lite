# Building AWS Greengrass Lite

This document details building and installing Greengrass from source.

At the current time, Greengrass Lite supports Linux targets using systemd as the
init system.

## Dependencies

This project uses the following third party library dependencies:

- openssl
- libcurl
- sqlite
- libyaml
- libsystemd
- libuuid
- libevent

On Ubuntu, these can be installed with:

```sh
sudo apt install libssl-dev libcurl4-openssl-dev libsqlite3-dev libyaml-dev \
  libsystemd-dev uuid-dev libevent-dev
```

## Build tools

To build the project, you will need the following build dependencies:

- GCC or Clang
- CMake (at least version 3.25)
- Make or Ninja
- pkg-config

On Ubuntu, these can be installed with:

```sh
sudo apt install build-essential pkg-config cmake
```

If your system's cmake is too old, you may need to build or download a newer
version. The provided `bootstrap-cmake.sh` script downloads a new cmake version
into `./build/cmake` and configures the project using that CMake with `./build`
as the build directory. If you do so, you can skip the configuring step below.

## Building

The project is configured with CMake in the standard way.

The following examples assume you are in the unpacked source directory, and you
are using `./build` as the build directory.

To make a release build configured for minimal size, run:

```sh
cmake -B build -D CMAKE_BUILD_TYPE=MinSizeRel
```

To set the install directory to a location other than the standard system
locations, set `CMAKE_INSTALL_PREFIX`. For testing, Greengrass Lite can be run
from the build directory without installing.

For example:

```sh
cmake -B build -D CMAKE_INSTALL_PREFIX=/opt/aws-greengrass-lite
```

To build then run `make`:

```sh
make -C build -j$(nproc)
```

To install to the system, run:

```sh
make -C build install
```

## Configuration flags

The following configuration flags may be set with cmake (with `-D`):

- `GGL_LOG_LEVEL`

  This can be set to `NONE`, `ERROR`, `WARN`, `INFO`, `DEBUG`, or `TRACE` to set
  the logging level.

- `GGL_IPC_AUTH_DISABLE`

  Set this flag to 1 to compile in debugging mode for IPC authentication. This
  will enable you to test components, bypassing auth, by using the desired
  component name as the SVCUID value. This mode is insecure and is only for
  testing purposes.
