# Greengrass Lite Sample Component

This component uses AWS IoT Greengrass SDK V2 to invoke Greengrass Lite
functionality. It can be deployed (or simply run) as a Greengrass component to
subscribe and publish a message to AWS IoT Core.

**Note**: The Greengrass Lite should be running on the device with the following
plugins: `iot_broker`, `local_broker`, `cli_server`, `ipc_server`,
`native_plugin`.

The rest of this readme assumes your gglite run directory is `/gglite_testing`.

## Build and Install

```sh
cmake -B build -DCMAKE_INSTALL_PREFIX=<install_path>
make -C build -j4 install
```

## Run without deployment

To run the component

```sh
AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT=/gglite_testing/gglite-ipc.socket SVCUID=<auth_token> ./build/sample-component <message>
```

## Run with Local Deployment

Make sure that you have the
[Greengrass CLI](https://github.com/aws-greengrass/aws-greengrass-cli) and you
followed the previous steps to install the component.

To deploy the component run

```bash
./greengrass-cli --ggcRootPath=/gglite_testing deployment create --recipeDir /path/to/recipes --artifactDir /path/to/artifacts --merge "com.example.SampleComponent=1.0.0"
```

You can find an example recipe
[here](recipes/com.example.SampleComponent-1.0.0.yaml).
