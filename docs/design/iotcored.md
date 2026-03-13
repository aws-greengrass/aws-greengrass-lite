# `iotcored` design

See [`iotcored` spec](../spec/executable/iotcored.md) for the public interface
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
be pulled from the Greengrass Nucleus Lite config library.

## MQTT Session Behavior

Greengrass Nucleus Lite connects to AWS IoT Core with `cleanSession = true`.
This differs from Greengrass Nucleus, which uses `cleanSession = false`
(persistent sessions).

With clean sessions:

- IoT Core does not persist subscriptions across disconnects. On reconnection,
  `iotcored` re-registers all active subscriptions from its local tracking.
- Messages published to subscribed topics while the device is offline are not
  queued by IoT Core and will be lost.

With persistent sessions (Greengrass Nucleus):

- IoT Core retains subscriptions and queues QoS 1 messages (up to service
  limits) for delivery when the device reconnects.

This behavioral difference affects components that rely on receiving messages
published during network interruptions.
