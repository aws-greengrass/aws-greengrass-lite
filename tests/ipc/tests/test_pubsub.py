import threading
import time
from unittest import mock

import pytest
from awsiot.greengrasscoreipc.model import (
    MessageContext,
    SubscriptionResponseMessage,
    PublishMessage,
    BinaryMessage,
    MQTTMessage,
    IoTCoreMessage,
)

from utils import StreamHandler

TIMEOUT = 10


#
# Greengrass Lite Publish
#
@pytest.mark.parametrize(
    "get_data",
    [
        ("topic", r"Hello World!"),
        ("my/topic", r"Sweet home!"),
        ("root/my/topic", r"Paper stub"),
    ],
)
def test_local_publish(ipc_client, get_data: tuple[str, str]):
    topic, message = get_data
    binary_message = BinaryMessage(message=bytes(message, "utf-8"))
    publish_message = PublishMessage(binary_message=binary_message)
    response = ipc_client.publish_to_topic_async(
        topic=topic, publish_message=publish_message)
    res = response.result(TIMEOUT)
    assert res is not None


@pytest.mark.parametrize(
    "get_data",
    [
        ("topic", "1", r"Hello World!"),
        ("my/topic", "1", r"Sweet home!"),
        ("root/my/topic", "1", r"Paper stub"),
    ],
)
def test_iot_core_publish(ipc_client, mqtt_client, get_data: tuple[str, str,
                                                                   str]):
    topic, qos, payload = get_data
    assert mqtt_client.subscribe_to_topic(topic, qos)
    response = ipc_client.publish_to_iot_core_async(topic_name=topic,
                                                    qos=qos,
                                                    payload=payload)
    result = response.result(timeout=TIMEOUT)
    assert result is not None
    message = mqtt_client.get_message()
    assert message.topic == topic
    assert message.payload.decode() == payload


#
# Greengrass Lite Subscribe
#
@pytest.mark.usefixtures("publish_to_local_topic")
@pytest.mark.parametrize("get_data", [("my/topic", r"Hello World!"),
                                      ("my/#", r"Hello World!")])
def test_local_subscribe(ipc_client, get_data: tuple[str, str]):
    topic: str
    message: str
    topic, message = get_data
    expected_response = SubscriptionResponseMessage()
    expected_response.binary_message = BinaryMessage(
        message=bytes(message, "utf-8"),
        context=MessageContext(topic="my/topic"))

    # mock the stream handler
    stream_handler = StreamHandler()
    event = threading.Event()
    stream_handler.on_stream_event = mock.Mock(return_value=None,
                                               side_effect=event.set())
    stream_handler.on_stream_error = mock.Mock(return_value=True)
    stream_handler.on_stream_closed = mock.Mock(return_value=None)

    # subscribe to given topic
    response, operation = ipc_client.subscribe_to_topic_async(
        topic=topic, stream_handler=stream_handler)
    result = response.result(timeout=TIMEOUT)
    assert result is not None
    print(f"Subscribed to topic {topic}")

    # callback called at least once with expected response
    event.wait(timeout=TIMEOUT)
    time.sleep(0.1)
    stream_handler.on_stream_event.assert_called_with(expected_response)

    # close the stream.
    operation.close()


@pytest.mark.usefixtures("publish_to_iot_topic")
@pytest.mark.parametrize("get_data", [("my/topic", "1", r"Hello World!"),
                                      ("my/#", "1", r"Hello World!")])
def test_iot_core_subscribe(ipc_client, get_data: tuple[str, str, str]):
    topic, qos, message = get_data
    expected_response = IoTCoreMessage()
    expected_response.message = MQTTMessage(
        topic_name="my/topic",
        payload=bytes(rf"""{{"message": "{message}"}}""", "utf-8"))

    # mock the stream handler
    stream_handler = StreamHandler()
    event = threading.Event()
    stream_handler.on_stream_event = mock.Mock(return_value=None,
                                               side_effect=event.set())
    stream_handler.on_stream_error = mock.Mock(return_value=True)
    stream_handler.on_stream_closed = mock.Mock(return_value=None)

    # subscribe to given iot topic
    response, operation = ipc_client.subscribe_to_iot_core_async(
        topic_name=topic, qos=qos, stream_handler=stream_handler)
    result = response.result(timeout=TIMEOUT)
    assert result is not None
    print(f"Subscribed to topic {topic} with QoS {qos}")

    # callback called at least once with expected response
    event.wait(timeout=TIMEOUT)
    time.sleep(1)
    stream_handler.on_stream_event.assert_called_with(expected_response)

    # close the stream
    operation.close()


#
# Greengrass Lite Pub/Sub
#
def test_local_subscribe_and_publish(ipc_client):
    topic: str = "my/topic"
    payload: str = r"Hello World!"
    expected_response = SubscriptionResponseMessage()
    expected_response.binary_message = BinaryMessage(
        message=bytes(payload, "utf-8"), context=MessageContext(topic=topic))

    # subscribe to a local topic
    stream_handler = StreamHandler()
    event = threading.Event()
    stream_handler.on_stream_event = mock.Mock(return_value=None,
                                               side_effect=event.set())
    stream_handler.on_stream_error = mock.Mock(return_value=True)
    stream_handler.on_stream_closed = mock.Mock(return_value=None)
    sub_response, operation = ipc_client.subscribe_to_topic_async(
        topic=topic, stream_handler=stream_handler)
    assert sub_response.result(timeout=TIMEOUT) is not None
    print(f"Subscribed to a local topic {topic}")

    # publish to a local topic
    binary_message = BinaryMessage(message=bytes(payload, "utf-8"))
    publish_message = PublishMessage(binary_message=binary_message)
    pub_response = ipc_client.publish_to_topic_async(
        topic=topic, publish_message=publish_message)
    assert pub_response.result(timeout=TIMEOUT) is not None
    print(f"Published {payload} to a local topic {topic}")

    # callback called with expected response
    event.wait(timeout=TIMEOUT)
    time.sleep(0.1)
    stream_handler.on_stream_event.assert_called_once_with(expected_response)

    # close the stream
    operation.close()


def test_iot_core_subscribe_and_publish(ipc_client):
    topic: str = "my/topic"
    qos: str = "1"
    payload: str = r"Hello World!"
    expected_response = IoTCoreMessage(
        message=MQTTMessage(topic_name=topic, payload=payload))

    # mock the stream handler
    stream_handler = StreamHandler()
    event = threading.Event()
    stream_handler.on_stream_event = mock.Mock(return_value=None,
                                               side_effect=event.set())
    stream_handler.on_stream_error = mock.Mock(return_value=True)
    stream_handler.on_stream_closed = mock.Mock(return_value=None)

    # subscribe to an iot topic
    sub_response, operation = ipc_client.subscribe_to_iot_core_async(
        topic_name=topic, qos=qos, stream_handler=stream_handler)
    assert sub_response.result(timeout=TIMEOUT) is not None
    print(f"Subscribed to topic {topic} with QoS {qos}")

    # publish to an iot topic
    pub_response = ipc_client.publish_to_iot_core_async(topic_name=topic,
                                                        qos=qos,
                                                        payload=payload)
    assert pub_response.result(timeout=TIMEOUT) is not None
    print(f"Published to topic {topic} with QoS {qos}")

    # callback called with expected response
    event.wait(timeout=TIMEOUT)
    time.sleep(0.1)
    stream_handler.on_stream_event.assert_called_with(expected_response)

    # close the stream
    operation.close()
