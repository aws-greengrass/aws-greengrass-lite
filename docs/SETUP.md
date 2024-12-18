# Setting up Greengrass Nucleus Lite

See the [build guide](INSTALL.md) to compile and install Greengrass Nucleus
Lite. If running locally for evaluation, you can use the provided container to
install in its own environment.

Greengrass Nucleus Lite executables will be available under `bin` in the build
directory.

When run, they may use the current working directory to store state. Use the
directory you intend to use as the Greengrass root path.

The following examples assume you are using `./build` as the build directory,
and `/var/lib/greengrass` as the Greengrass root path.

## Dependencies

This project uses the following third party library dependencies:

| Library         | Minimum Version Required |
| --------------- | ------------------------ |
| openssl         | 3.0                      |
| libcurl         | 7.86                     |
| sqlite          | 3.46.0                   |
| libyaml         | 0.2.2                    |
| libsystemd      | 249                      |
| liburiparser    | 0.9.6                    |
| libuuid         | 2.37.2                   |
| libevent        | 2.1.12                   |
| libzip          | 1.7.3                    |
| libcgroup-tools | 2.0                      |

## Configuring Greengrass

You may configure a single device with the instruction below or a fleet of
devices with the steps from [Fleet Provisioning guide](Fleet-provisioning.md).
Choose one or the other.

To configure Greengrass, you will need a config YAML file, in the same format as
the Classic nucleus config. An example config file is available in
[`docs/examples/sample_nucleus_config.yaml`](examples/sample_nucleus_config.yaml).
If this is the first time you are creating a GG device, please follow the
instruction in the [TES setup instructions](./TES.md) to get a role alias,
thing, certificate, private key, and endpoints for your device.

Make a copy of the [sample configuration](./examples/sample_nucleus_config.yaml)
as `config.yaml`.

```sh
cp docs/examples/sample_nucleus_config.yaml ./config.yaml
```

Configure the following in your config file

- privateKeyPath: Path to private key for the Thing
- certificateFilePath: Path to Thing certificate
- rootCaPath: Path to Amazon Root CA certificate
- rootPath: Absolute path to the Greengrass rootpath directory
- thingName: Name of the Thing
- awsRegion: The AWS region with the Thing
- iotCredEndpoint: The IoT Core endpoint
- iotDataEndpoint: The IoT Core endpoint
- iotRoleAlias: The name of the role alias for accessing TES
- posixUser: Colon separated user/group that generic components should run as

`posixUser` must be set to a valid user and group. If no colon and group is
provided, the user's default group is used. If not running Greengrass as root,
set this to the user Greengrass is running as.

To initialize the config, initial configuration will need to be present either
as `/etc/greengrass/config.yaml`, and/or in one or more files in
`/etc/greengrass/config.d/`.

The config daemon will initially load `/etc/greengrass/config.yaml` and then
update the initial configuration with any other config files present in
`/etc/greengrass/config.d/`. Copy your configuration file to the above directory
( Note: you might need to run with `sudo` in case you are getting
`permission denied` error) -

```sh
mkdir -p /etc/greengrass
cp ./config.yaml /etc/greengrass/config.yaml
```

## Running the nucleus

To enable and run all the Greengrass Nucleus Lite core services for testing, run
the `run_nucleus` script available in the source directory.

```sh
./misc/run_nucleus
```

All core services will be reported under the greengrass-lite target. View their
statuses with `systemctl status --with-dependencies greengrass-lite.target`

Entire system logs can be viewed with `journalctl -a`. Individual service logs
can be viewed with `journalctl -a -t <service-name>` (e.g.
`journalctl -a -t ggdeploymentd` to view deployment logs).

To stop Greengrass Nucleus Lite run `systemctl stop greengrass-lite.target`

## Performing a local deployment

To do a local deployment with the Greengrass Nucleus Lite CLI, you will need a
directory with the component's recipe and a directory with the component's
artifacts. See the Greengrass component documentation for writing your own
Greengrass component.

Assuming you place these in `~/sample-component`, and that your component has a
single artifact named `hello_world.py`, the layout should be similar to the
following:

```
~/sample-component
├── artifacts
│   └── com.example.SampleComponent
│       └── 1.0.0
│           └── hello_world.py
└── recipes
    └── com.example.SampleComponent-1.0.0.yaml
```

With the above, you can start a local deployment with:

```sh
./build/bin/ggl-cli deploy --recipe-dir ~/sample-component/recipes \
  --artifacts-dir ~/sample-component/artifacts \
  --add-component com.example.SampleComponent=1.0.0
```

## Local-Deploying the sample Hello World component

See the
[com.example.LiteHelloWorld component README](../hello-world-component/README.md)
