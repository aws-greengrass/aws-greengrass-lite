import threading
from typing import override

import paho.mqtt.client as mqtt
from awsiot.eventstreamrpc import Shape, StreamResponseHandler


class MqttClient:

    def __init__(self, endpoint: str, root_ca_path: str, cert_path: str,
                 key_path: str):
        self._client = mqtt.Client(protocol=mqtt.MQTTv5, client_id="test")

        # enable TLS for secure connection
        self._client.tls_set(
            ca_certs=root_ca_path,
            certfile=cert_path,
            keyfile=key_path,
            tls_version=mqtt.ssl.PROTOCOL_TLS_CLIENT,
        )

        self.__endpoint = endpoint

        # set username and password
        self._client.username_pw_set("ggtest", "pa$sw0rd")

        # event for tracking callbacks
        self.__event = threading.Event()

    def connect(self):

        def on_connect(client, userdata, flags, rc, props):
            print("Connection established with iot core")
            self.__event.set()

        self._client.on_connect = on_connect
        self._client.connect(host=self.__endpoint, port=8883, keepalive=60)
        self._client.loop_start()
        self._wait(5)
        return self._client.is_connected()

    def disconnect(self):
        self._client.on_disconnect = (
            lambda client, userdata, rc, props: self.__event.set())
        self._client.disconnect()
        self._wait(5)
        self._client.loop_stop()
        print("Successfully disconnected mqtt client")

    def _wait(self, timeout: int):
        print("Waiting for the response from server...")
        self.__event.wait(timeout=timeout)
        if not self.__event.is_set():
            print("Request timed out!")
        else:
            self.__event.clear()

    def subscribe_to_topic(self, topic: str, qos: str) -> bool:
        success = False
        if not self._client.is_connected():
            return False

        def on_subscribe(client, userdata, mid, rcs, props):
            nonlocal success
            for reason in rcs:
                if reason.value not in [0, 1, 2]:
                    success = False
                    print(f"MQTT Subnack: {reason}")
                    break
                else:
                    print(f"Subscribed with {reason}")
                    success = True
            self.__event.set()

        self._client.on_subscribe = on_subscribe
        self._client.subscribe(topic=topic, qos=int(qos))
        self._wait(10)
        return success

    def get_message(self):
        message: mqtt.MQTTMessage = None

        def on_message(client, userdata, msg):
            nonlocal message
            message = msg
            self.__event.set()

        self._client.on_message = on_message
        self._wait(5)
        return message

    def publish_to_topic(self):
        pass


class StreamHandler(StreamResponseHandler):

    @override
    def on_stream_event(self, event: Shape) -> None:
        pass

    @override
    def on_stream_error(self, error: Exception) -> bool:
        pass

    @override
    def on_stream_closed(self) -> None:
        pass
