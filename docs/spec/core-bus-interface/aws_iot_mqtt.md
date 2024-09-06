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

### Response

- [aws-iot-mqtt-subscribe-4] Subscription responses are maps containing `topic`
  and `payload` keys, each of which have values of type buffer.
