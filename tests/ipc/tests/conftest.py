import json
import os
import threading
import time

import boto3
import pytest
import yaml
from awsiot.greengrasscoreipc.clientv2 import GreengrassCoreIPCClientV2
from awsiot.greengrasscoreipc.model import PublishMessage, BinaryMessage

from utils import MqttClient


@pytest.fixture(scope="session", autouse=True)
def config_data(request):
    config_path: str = request.config.getoption("--config-file")
    with open(config_path, "r") as f:
        try:
            config_data = yaml.safe_load(f)
            yield config_data
        except yaml.YAMLError as exc:
            print(f"Unable to parse configuration file: {exc}")


@pytest.fixture(scope="session", autouse=True)
def set_env(config_data):
    if not config_data["svcuid"]:
        raise ValueError("svcuid is required")
    else:
        os.environ["SVCUID"] = config_data["svcuid"]
    if not os.path.exists(config_data["socket_path"]):
        raise ValueError("socket_path is missing or do not exist")
    else:
        os.environ[
            "AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT"] = config_data[
                "socket_path"]


@pytest.fixture(scope="session", autouse=True)
def ipc_client(set_env):
    client = GreengrassCoreIPCClientV2()
    yield client
    client.close()


@pytest.fixture(scope="session", autouse=True)
def mqtt_client(config_data):
    client = MqttClient(
        config_data["endpoint"],
        config_data["root_ca_path"],
        config_data["cert_path"],
        config_data["key_path"],
    )
    if client.connect():
        yield client
    else:
        print("Connection timed out")
    client.disconnect()


@pytest.fixture
def get_data(request):
    yield request.param


@pytest.fixture(scope="function")
def publish_to_iot_topic():
    event = threading.Event()

    def publish_to_topic():
        # Initialize the iot client
        client = boto3.client("iot-data", "us-west-2")

        # Specify the topic you want to publish
        iot_topic = "my/topic"

        print("Started publishing on topic ", iot_topic)
        while not event.is_set():
            # Create a payload
            payload = {"message": r"Hello World!"}
            _ = client.publish(topic=iot_topic,
                               qos=1,
                               payload=json.dumps(payload))
            time.sleep(5)

    # create a thread for publishing
    publish_thread = threading.Thread(target=publish_to_topic)
    publish_thread.start()
    yield
    event.set()
    publish_thread.join()
