# `SubscribeToIoTCoreConnectionStatus` IPC command

The `SubscribeToIoTCoreConnectionStatus` IPC command subscribes a component to
the status of the AWS IoT Core MQTT connection using the `connection_status`
method of the `aws_iot_mqtt` core-bus interface provided by `iotcored`.

- [subscribe-to-iot-core-connection-status-1] It will be invoked with the topic
  `SubscribeToIoTCoreConnectionStatus`.
- [subscribe-to-iot-core-connection-status-2] It does not require an access
  control policy in the recipe to work, as it is an informational local-only
  operation which exposes no MQTT topic data.
- [subscribe-to-iot-core-connection-status-3] The current connection status is
  delivered as the first stream event immediately after the subscription is
  accepted.
- [subscribe-to-iot-core-connection-status-4] A stream event is delivered on
  each subsequent connect/disconnect transition of the MQTT connection.
- [subscribe-to-iot-core-connection-status-5] The operation response is sent
  before any stream event, per the eventstream protocol requirements.

## Parameters

This operation takes no parameters; the request payload is an empty object.

## Response

- [subscribe-to-iot-core-connection-status-resp-1] On success, returns an empty
  `SubscribeToIoTCoreConnectionStatusResponse`, followed by a stream of
  `IoTCoreConnectionStatusEvent` events.
  - [subscribe-to-iot-core-connection-status-resp-1.1] Each stream event is a
    map containing a `connectionStatusEvent` key, whose value is a map
    containing a `status` key.
  - [subscribe-to-iot-core-connection-status-resp-1.2] `status` is a buffer
    containing either `CONNECTED` or `DISCONNECTED`.
- [subscribe-to-iot-core-connection-status-resp-2] On failure, returns a map
  containing `message`, `_service`, `_message` and `_errorCode`.
  - [subscribe-to-iot-core-connection-status-resp-2.1] `ServiceError` is
    returned if the core-bus subscription cannot be established.

## On the wire

```
structure SubscribeToIoTCoreConnectionStatusRequest {}

structure SubscribeToIoTCoreConnectionStatusResponse {
    connectionStatusEvent: IoTCoreConnectionStatusEvent
}

@streaming
union IoTCoreConnectionStatusEvent {
    /// The connection status event.
    connectionStatusEvent: ConnectionStatusEvent
}

structure ConnectionStatusEvent {
    /// The connection status.
    @required
    status: ConnectionStatus
}

@enum([
    {
        value: "CONNECTED",
        name: "CONNECTED"
    },
    {
        value: "DISCONNECTED",
        name: "DISCONNECTED"
    }
])
string ConnectionStatus

operation SubscribeToIoTCoreConnectionStatus {
    input: SubscribeToIoTCoreConnectionStatusRequest,
    output: SubscribeToIoTCoreConnectionStatusResponse,
    errors: [ServiceError]
}
```

## Examples

Case 1: Subscribe while the device is connected to AWS IoT Core

- Request: `{}`
- Response: `{}`
- First stream event (current status):
  `{"connectionStatusEvent":{"status":"CONNECTED"}}`

Case 2: MQTT connection is interrupted after subscribing

- Stream event: `{"connectionStatusEvent":{"status":"DISCONNECTED"}}`

Case 3: MQTT connection resumes

- Stream event: `{"connectionStatusEvent":{"status":"CONNECTED"}}`

### RAW on wire:

```shell
Packet from client:
  Headers:
    [:content-type] => String(StrBytes { bytes: b"application/json" })
    [service-model-type] => String(StrBytes { bytes: b"aws.greengrass#SubscribeToIoTCoreConnectionStatusRequest" })
    [:message-type] => Int32(0)
    [:message-flags] => Int32(0)
    [:stream-id] => Int32(1)
    [operation] => String(StrBytes { bytes: b"aws.greengrass#SubscribeToIoTCoreConnectionStatus" })
  Value: {}
Packet from server (response):
  Headers:
    [:content-type] => String(StrBytes { bytes: b"application/json" })
    [service-model-type] => String(StrBytes { bytes: b"aws.greengrass#SubscribeToIoTCoreConnectionStatusResponse" })
    [:message-type] => Int32(0)
    [:message-flags] => Int32(0)
    [:stream-id] => Int32(1)
  Value: {}
Packet from server (stream event):
  Headers:
    [:content-type] => String(StrBytes { bytes: b"application/json" })
    [service-model-type] => String(StrBytes { bytes: b"aws.greengrass#IoTCoreConnectionStatusEvent" })
    [:message-type] => Int32(0)
    [:message-flags] => Int32(0)
    [:stream-id] => Int32(1)
  Value: {"connectionStatusEvent":{"status":"CONNECTED"}}
```
