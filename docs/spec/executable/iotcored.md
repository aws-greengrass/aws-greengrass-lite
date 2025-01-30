# `iotcored` spec

`iotcored` implements an MQTT spooler daemon for AWS IoT Core client
applications, and provides the `aws_iot_mqtt` core-bus interface. It enables
multiple processes to share an MQTT connection using shared credentials, handled
by the `iotcored` daemon.

For information on MQTT, see <https://mqtt.org/mqtt-specification/>.

### 1.0 general

- [iotcored-1.1] The daemon handles the `aws_iot_mqtt` core-bus interface.
- [iotcored-1.2] The daemon connects to AWS IoT Core on startup and maintains
  the connection.
- [iotcored-1.3] On disconnect, the daemon tries to reconnect indefinitely with
  backoff.
- [iotcored-1.4] On auth failure, the daemon exits with error.

### 2.0 autoscaling

- [iotcored-2.1] When the number of subscriptions exceeds the AWS IoT Core
  per-connection maximum, additional connections will be used.
- [iotcored-2.2] The maximum number of auto-scaling connections may be set by a
  compile time value.
- [iotcored-2.3] The maximum number of auto-scaling connections may be set by a
  command line argument.
- [iotcored-2.4] The maximum number of auto-scaling connections may be set by a
  configuration value.
- [iotcored-2.5] The maximum value of the autoscaling connection count is set
  according to the following priority:

1. command line argument (optional)
2. configuration value (optional)
3. compile time value. (required)

- [iotcored-2.6] The maximum value of the autoscaling connection count shall not
  exceed the compile time value.

### 3.0 endpoints

The endpoint refers to the AWS IoT Core address used to establish an MQTT
connection.

- [iotcored-3.1] The AWS endpoint target may be specified by an optional command
  line argument.
- [iotcored-3.2] The AWS endpoint target shall be specified by the configuration
  system if no command line argument is present.
- [iotcored-3.3] `iotcored` shall exit with a error forwarded from the
  configuration system if no endpoint is available.
- [iotcored-3.4] The endpoint target command line argument shall be `--endpoint`
  or `-e`.
- [iotcored-3.5] The configuration path shall be
  `services/aws.greengrass.NucleusLite/configuration/iotDataEndpoint`.

### 4.0 client id

The client id refers to the device identification presented to AWS IoT core.
This id must be unique. The client id is also called the `thing name` in IoT
Core.

- [iotcored-4.1] The client id may be specified by an optional command line
  argument.
- [iotcored-4.2] The client id shall be specified by the configuration system.
- [iotcored-4.3] The configuration path shall be `system/thingName`.
- [iotcored-4.4] The client id will get the suffix `_{N}` during autoscaling.
  The value of `N` is the autoscaling connection number.
- [iotcored-4.5] The client id for connection 0 shall be the bare client id
  without a connection number suffix.
- [iotcored-4.6] The client id argument may be provided by `--id` or `-i`.

### 5.0 rootca

The rootca is a PEM file used for authenticating AWS IoT Core to the device.

- [iotcored-5.1] The rootca argument specifies the path to the root CA
  certificate file.
- [iotcored-5.2] The rootca must be in PEM format.
- [iotcored-5.4] The rootca argument may be provided by `--rootca` or `-r`.
- [iotcored-5.6] The rootca argument is optional.
- [iotcored-5.7] The configuration path shall be `system/rootCaPath`.

### 6.0 client certificate

The client certificate is a PEM file used for authenticating the device AWS IoT
Core.

- [iotcored-6.1] The certificate file path argument specifies the path to the
  client certificate PEM file.
- [iotcored-6.2] The certificate must be in PEM format.
- [iotcored-6.2] The cert argument may be provided by `--cert` or `-c`.
- [iotcored-6.3] The cert argument is optional.
- [iotcored-6.4] The certificate file path shall be specified by the
  configuration system.
- [iotcored-6.5] The configuration path shall be `system/certificateFilePath`.

### 7.0 Private Key

The private key is used to generate keys during TLS and certificate rotation
operations.

- [iotcored-7.1] The key argument specifies the path to the private key PEM
  file.
- [iotcored-7.2] The key must be in PEM format.
- [iotcored-7.3] The key argument can be provided by `--key` or `-k`.
- [iotcored-7.4] The key argument is optional.
- [iotcored-7.5] The configuration path shall be `system/privateKeyPath`.
