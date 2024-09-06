# `iotcored` spec

`iotcored` implements an MQTT spooler daemon for AWS IoT Core client
applications, and provides the `aws_iot_mqtt` core-bus interface. It enables
multiple processes to share an MQTT connection using shared credentials, handled
by the `iotcored` daemon.

For information on MQTT, see <https://mqtt.org/mqtt-specification/>.

- [iotcored-1] The daemon handles the `aws_iot_mqtt` core-bus interface.
- [iotcored-2] The daemon connects to AWS IoT Core on startup and maintains the
  connection.
- [iotcored-3] On disconnect, the daemon tries to reconnect indefinitely with
  backoff.
- [iotcored-4] On auth failure, the daemon exits with error.
- [iotcored-5] When the number of subscriptions exceeds the AWS IoT Core
  per-connection maximum, additional connections will be used.
- [iotcored-6] A compile time flag is available to set the maximum number of
  connections used (default 1).
- [iotcored-7] Configuration values will be pulled from the config over core-bus
  unless overridden by a CLI parameter.

## Config keys

TODO

## CLI parameters

### endpoint

- [iotcored-param-endpoint-1] The endpoint argument configures the endpoint the
  daemon connects to.
- [iotcored-param-endpoint-2] The endpoint argument can be provided by
  `--endpoint` or `-e`.
- [iotcored-param-endpoint-3] The endpoint argument is optional.

### id

- [iotcored-param-id-1] The id argument specifies the MQTT client identifier to
  use. This may be suffixed with `_{N}`, for some integer `N`, if additional
  connections are needed due to exceeding the maximum number of subscriptions
  allowed by AWS IoT Core per connection.
- [iotcored-param-id-2] The id argument can be provided by `--id` or `-i`.
- [iotcored-param-id-3] The id argument is optional.

### rootca

- [iotcored-param-rootca-1] The rootca argument specifies the path to the root
  CA certificate file used to validate the AWS IoT Core endpoint, in PEM format.
  Ensure that it has the trailing newline character, as some SSL libraries do
  not support PEM files with missing trailing newline.
- [iotcored-param-rootca-2] The rootca argument can be provided by `--rootca` or
  `-r`.
- [iotcored-param-rootca-3] The rootca argument is optional.

### cert

- [iotcored-param-cert-1] The cert argument specifies the path to the client
  certificate PEM file to be used for TLS mutual auth with AWS IoT Core to
  authenticate the device.
- [iotcored-param-cert-2] The cert argument can be provided by `--cert` or `-c`.
- [iotcored-param-cert-3] The cert argument is optional.

### key

- [iotcored-param-key-1] The key argument specifies the path to the private key
  PEM file corresponding to the `cert` argument.
- [iotcored-param-key-2] The key argument can be provided by `--key` or `-k`.
- [iotcored-param-key-3] The key argument is optional.
