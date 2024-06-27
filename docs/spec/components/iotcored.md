# `iotcored` spec

`iotcored` implements an MQTT spooler daemon for AWS IoT Core client
applications. It enables multiple processes to share an MQTT connection using
shared credentials, handled by the `iotcored` daemon.

For information on MQTT, see <https://mqtt.org/mqtt-specification/>.

The current implementation uses MQTT 3.1.1 and will be updated to use MQTT 5 in
the future.

- [iotcored-1] The daemon connects to AWS IoT Core on startup and maintains the
  connection.
- [iotcored-2] On disconnect, the daemon tries to reconnect indefinitely with
  backoff.
- [iotcored-3] On auth failure, the daemon exits with error.
- [iotcored-4] When the number of subscriptions exceeds the AWS IoT Core
  per-connection maximum, additional connections will be used.
- [iotcored-5] A compile time flag is available to set the maximum number of
  connections used (default 1).

## CLI parameters

### endpoint

- [iotcored-param-endpoint-1] The endpoint argument configures the endpoint the
  daemon connects to.
- [iotcored-param-endpoint-2] The endpoint argument can be provided by
  `--endpoint` or `-e`.
- [iotcored-param-endpoint-3] The endpoint argument is required.

### id

- [iotcored-param-id-1] The id argument specifies the MQTT client identifier to
  use. This may be suffixed with `_{N}`, for some integer `N`, if additional
  connections are needed due to exceeding the maximum number of subscriptions
  allowed by AWS IoT Core per connection.
- [iotcored-param-id-2] The id argument can be provided by `--id` or `-i`.
- [iotcored-param-id-3] The id argument is required.

### rootca

- [iotcored-param-rootca-1] The rootca argument specifies the path to the root
  CA certificate file used to validate the AWS IoT Core endpoint, in PEM format.
  Ensure that it has the trailing newline character, as some SSL libraries do
  not support PEM files with missing trailing newline.
- [iotcored-param-rootca-2] The rootca argument can be provided by `--rootca` or
  `-r`.
- [iotcored-param-rootca-3] The rootca argument is required.

### cert

- [iotcored-param-cert-1] The cert argument specifies the path to the client
  certificate PEM file to be used for TLS mutual auth with AWS IoT Core to
  authenticate the device.
- [iotcored-param-cert-2] The cert argument can be provided by `--cert` or `-c`.
- [iotcored-param-cert-3] The cert argument is required.

### key

- [iotcored-param-key-1] The key argument specifies the path to the private key
  PEM file corresponding to the `cert` argument.
- [iotcored-param-key-2] The key argument can be provided by `--key` or `-k`.
- [iotcored-param-key-3] The key argument is required.

## Environment Variables

## Core Bus API

Each of the APIs below take a single map as the argument to the call, with the
key-value pairs described by the parameters listed in their respective sections.

### publish

The publish method sends an MQTT publish packet to AWS IoT Core.

- [iotcored-bus-publish-1] `topic` is a required parameter of type buffer.
  - [iotcored-bus-publish-1.1] `topic` must contain the MQTT topic on which to
    publish.
- [iotcored-bus-publish-2] `payload` is an optional parameter of type buffer.
  - [iotcored-bus-publish-2.1] `payload` is the MQTT publish payload.
  - [iotcored-bus-publish-2.2] If `payload` is not provided, publish sends an
    empty payload.
- [iotcored-bus-publish-3] `qos` is an optional parameter of type integer.
  - [iotcored-bus-publish-3.1] `qos` sets the MQTT QoS for the publish.
  - [iotcored-bus-publish-3.2] QoS 0 is the default when `qos` is not provided.
  - [iotcored-bus-publish-3.3] QoS 2 is not supported (matching AWS IoT Core
    limitations).
- [iotcored-bus-publish-4] `iotcored` will rate limit publishes according to AWS
  IoT Core limits.
  - [iotcored-bus-publish-4.1] Exceeding the limit does not result in an error.
  - [iotcored-bus-publish-4.2] Publishes are limited to 100 per second.
  - [iotcored-bus-publish-4.3] Publishes are limited to 512Kb per second.

### subscribe

The subscribe method sends an MQTT subscription request packet to AWS IoT Core.

- [iotcored-bus-subscribe-1] `topic_filter` is a required parameter of type
  buffer or type list of buffers.
  - [iotcored-bus-subscribe-1.1] `topic_filter` must contain the topic filters
    to subscribe to.
- [iotcored-bus-subscribe-2] `qos` is an optional parameter of type integer.
  - [iotcored-bus-subscribe-2.1] `qos` sets the MQTT QoS for the subscription.
  - [iotcored-bus-subscribe-2.2] QoS 0 is the default when `qos` is not
    provided.
  - [iotcored-bus-subscribe-2.3] QoS 2 is not supported (matching AWS IoT Core
    limitations).
- [iotcored-bus-subscribe-3] `response_endpoint` is the bus endpoint to which
  subscription responses are sent. `iotcored` calls the `iotcored_response`
  method on the given endpoint for each matching incoming publish.
  - [iotcored-bus-subscribe-3.1] Subscription responses are maps containing
    `topic` and `payload` keys, both of which have values of type buffer.
