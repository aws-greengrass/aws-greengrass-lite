# `iotcored` design

See [`iotcored` spec](../spec/components/iotcored.md) for the public interface
for `iotcored`.

The implementation of `iotcored` is split into three parts:

- main
- MQTT
- TLS

The TLS code exports a interface to abstract out the specifics of the used TLS
implementation, and allow for swapping out the TLS library used. It receives the
program arguments, and may use the program arguments and config library to
obtain necessary credentials and configuration. This includes the root CA,
client certificate, AWS IoT Core endpoint, and so on. The implementation
provided uses OpenSSL.

The MQTT code uses the TLS interface to provide an interface for connecting to
AWS IoT Core, and publishing and subscribing over that MQTT connection. The
provided implementation uses the coreMQTT library to implement the interface.

The main code sets up a core bus listener and handles incoming publish/subscribe
calls.

## Configuration

The daemon requires configuration for connecting to AWS IoT Core, including the
endpoint and device credentials.

These may be passed as command line parameters; if they are not the values will
be pulled from the Greengrass lite config library.
