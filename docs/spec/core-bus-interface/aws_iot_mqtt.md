# `aws_iot_mqtt` interface

The `aws_iot_mqtt` core-bus interface provides functionality for communicating
with AWS IoT Core over MQTT.

The current interface supports MQTT 3.1.1 functionality and will be extended to
support additional MQTT 5 features in the future.

Each method in the interface is described below.

## publish

The publish method sends an MQTT publish packet to AWS IoT Core.

- [aws-iot-mqtt-publish-1] `publish` can be invoked with call or notify.
- [aws-iot-mqtt-publish-2] Publishes will be rate-limited according to AWS IoT
  Core limits.
  - [aws-iot-mqtt-publish-2.1] Exceeding the limit does not result in an error.
  - [aws-iot-mqtt-publish-2.2] Publishes are limited to 100 per second.
  - [aws-iot-mqtt-publish-2.3] Publishes are limited to 512Kb per second.

### Parameters

- [aws-iot-mqtt-publish-3] `topic` is a required parameter of type buffer.
  - [aws-iot-mqtt-publish-3.1] `topic` must contain the MQTT topic on which to
    publish.
- [aws-iot-mqtt-publish-4] `payload` is an optional parameter of type buffer.
  - [aws-iot-mqtt-publish-4.1] `payload` is the MQTT publish payload.
  - [aws-iot-mqtt-publish-4.2] If `payload` is not provided, publish sends an
    empty payload.
- [aws-iot-mqtt-publish-5] `qos` is an optional parameter of type integer.
  - [aws-iot-mqtt-publish-5.1] `qos` sets the MQTT QoS for the publish.
  - [aws-iot-mqtt-publish-5.2] QoS 0 is the default when `qos` is not provided.
  - [aws-iot-mqtt-publish-5.3] QoS 0 and 1 are supported (AWS IoT Core does not
    support QoS 2).

### Response

This method does not provide a response object.

## subscribe

The subscribe method sets up a MQTT subscription to AWS IoT Core, and returns
the subscription responses.

- [aws-iot-mqtt-subscribe-1] `subscribe` can be invoked with subscribe.

### Parameters

- [aws-iot-mqtt-subscribe-2] `topic_filter` is a required parameter of type
  buffer or type list of buffers.
  - [aws-iot-mqtt-subscribe-2.1] `topic_filter` must contain the topic filters
    to subscribe to.
- [aws-iot-mqtt-subscribe-3] `qos` is an optional parameter of type integer.
  - [aws-iot-mqtt-subscribe-3.1] `qos` sets the MQTT QoS for the subscription.
  - [aws-iot-mqtt-subscribe-3.2] QoS 0 is the default when `qos` is not
    provided.
  - [aws-iot-mqtt-subscribe-3.3] QoS 0 and 1 are supported (AWS IoT Core does
    not support QoS 2).
- [aws-iot-mqtt-subscribe-4] `virtual` is an optional parameter of type boolean.
  - [aws-iot-mqtt-subscribe-4.1] `virtual` registers the topic filters for
    on-device routing only. No MQTT subscribe is sent to AWS IoT Core.
  - [aws-iot-mqtt-subscribe-4.2] `false` is the default when `virtual` is not
    provided.
  - [aws-iot-mqtt-subscribe-4.3] A virtual subscription receives any message
    that reaches the device on a matching topic. This covers messages that AWS
    IoT Core delivers without a subscription, such as direct messages addressed
    to the device by client ID, and responses sent automatically in reply to a
    publish.
  - [aws-iot-mqtt-subscribe-4.4] `qos` has no effect when `virtual` is `true`.
  - [aws-iot-mqtt-subscribe-4.5] Closing a virtual subscription sends no MQTT
    unsubscribe.
  - [aws-iot-mqtt-subscribe-4.6] A virtual subscription is not subscribed again
    after an MQTT reconnection. It continues to receive without any action,
    because it never depended on an MQTT subscription.
  - [aws-iot-mqtt-subscribe-4.7] A virtual subscription does not keep an MQTT
    subscription alive for a topic filter it shares with a non-virtual
    subscription.

### Response

- [aws-iot-mqtt-subscribe-5] Subscription responses are maps containing `topic`
  and `payload` keys, each of which have values of type buffer.
