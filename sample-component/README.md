# Greengrass Lite IPC Client Template

This template uses AWS IoT Greengrass SDK V2 to invoke Greengrass functionality.
It can be deployed (or simply run) as a Greengrass component to publish a
message to Greengrass local pubsub and to publish a message to AWS IoT Core.

**Note**: The Greengrass core should be running on the device.

## Build and Run

```sh
cmake -B build
make -C build
AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT=/tmp/gglite-ipc.socket SVCUID=auth_token ./build/sample-component
```
