# Setting up Greengrass Lite

See the [build guide](INSTALL.md) to compile Greengrass Lite. You do not need to
install it.

Greengrass Lite executables will be available under `bin` in the build
directory.

When run, they may use the current working directory to store state. Use the
directory you intend to use as the Greengrass rootpath.

The following examples assume you are using `./build` as the build directory,
and `./run` as the Greengrass rootpath.

## Configuring Greengrass

To configure Greengrass, you will need a config YAML file, in the same format as
the Classic nucleus config. An example config file is available in
[`doc/examples/sample_nucleus_config.yml`](examples/sample_nucleus_config.yml).

A working installation will require a thing certificate package.

First go to AWS IoT Core and create a thing.

Make a copy of the sample configuration.

Configure the following in your config file

- privateKeyPath: Path to private key for the Thing
- certificateFilePath: Path to Thing certificate
- thingName: Name of the Thing
- rootpath: Absolute path to the Greengrass rootpath directory
- awsRegion: The AWS region with the Thing
- iotCredEndpoint: The IoT Core endpoint
- iotDataEndpoint: The IoT Core endpoint
- posixUser: Colon separated user/group that generic components should run as

`posixUser` must be set to a valid user and group. If no colon and group is
provided, the user's default group is used. If not running Greengrass as root,
set this to the user Greengrass is running as.

The following examples assume you have made a copy in `./run/init_config.yml`
and configured the required fields in it.

To load the config, you will need to run the config daemon and run the
configuration script.

In one shell, run the config daemon:

```sh
cd ./run
../build/bin/ggconfigd
```

In another shell, run the config script

```sh
cd ./run
../build/bin/ggl-config-init --config ./init_config.yml
```

You can then kill the config daemon.

If you already have a Greengrass Lite system running, and want to update the
config values, you don't need to run another copy of the config daemon, just run
`ggl-config-init`.

## Running the nucleus

To run all the Greengrass Lite core services for testing, enter a working dir
and run `run_nucleus`:

```sh
cd ./run
export GGL_RECIPE_TO_UNIT_PATH=../recipe2unit_python/main.py
../build/bin/run_nucleus
```

## Running Deployments (Temporary)

There is a temporary dependency on a python script for deployments to work. The
following steps will install needed dependencies on ubuntu:

- Install python3 using `sudo apt install python3`
- Install python pip using `sudo apt install python3-pip`
- Install script requirements using
  `python3 -m pip install -r ./recipe2unit_python/requirements`
