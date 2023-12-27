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

    # publish to given topic
    binary_message = BinaryMessage(message=bytes(message, "utf-8"))
    publish_message = PublishMessage(binary_message=binary_message)
    response = ipc_client.publish_to_topic_async(
        topic="my/topic", publish_message=publish_message)

    # callback called at least once with expected response
    event.wait(timeout=TIMEOUT)
    time.sleep(0.1)
    stream_handler.on_stream_event.assert_called_with(expected_response)

    # close the stream.
    operation.close()


@pytest.mark.parametrize("get_data", [("my/topic", "1", r"Hello World!"),
                                      ("my/#", "1", r"Hello World!")])
def test_iot_core_subscribe(ipc_client, get_data: tuple[str, str, str]):
    topic, qos, message = get_data
    expected_response = IoTCoreMessage()
    expected_response.message = MQTTMessage(topic_name="my/topic",
                                            payload=bytes(
                                                f"{message}", "utf-8"))

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

    # publish to matching topic
    response = ipc_client.publish_to_iot_core_async(topic_name="my/topic",
                                                    qos=qos,
                                                    payload=message)

    # callback called at least once with expected response
    event.wait(timeout=TIMEOUT)
    time.sleep(1)
    stream_handler.on_stream_event.assert_called_with(expected_response)

    # close the stream
    operation.close()
