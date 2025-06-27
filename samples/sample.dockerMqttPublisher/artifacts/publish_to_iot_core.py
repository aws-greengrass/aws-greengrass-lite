from time import sleep
import awsiot.greengrasscoreipc.clientv2 as clientV2
from os import environ
from sys import stderr
import logging

logger = logging.getLogger('publish_to_iot_core')
logging.addLevelName(logging.ERROR, '\033[1;31mE')
logging.addLevelName(logging.WARNING, '\033[1;33mW')
logging.addLevelName(logging.INFO, '\033[0;32mI')
logging.addLevelName(logging.DEBUG, '\033[0;34mD')
logging.addLevelName(logging.CRITICAL, '\033[1;31mC')
logging.addLevelName(logging.NOTSET, '\033[0;37m?')
logging.basicConfig(
    stream=stderr,
    level=logging.INFO,
    format='%(levelname)s[%(name)s] %(filename)s:%(lineno)d: %(message)s\033[0m'
)


def main():
    topic = environ.get('MQTT_TOPIC', default='my/topic')
    payload = environ.get('MQTT_PAYLOAD', default='Hello, World')
    qos = int(environ.get('MQTT_QOS', default='0'))
    if (qos < 0) or (qos > 2):
        logging.warning(f"Invalid QoS: {qos}. Falling back to 0.")
        qos = 0
    qos = str(qos)

    logger.info('Creating IPC client')
    ipc_client = clientV2.GreengrassCoreIPCClientV2()
    for i in range(5):
        logger.info(
            f'Preparing to publish {payload} to {topic} with QoS={qos}')
        resp = ipc_client.publish_to_iot_core(topic_name=topic,
                                              payload=payload,
                                              qos=qos)
        logger.info('Sleeping...')
        sleep(5)
    logger.info(f'Preparing to publish {payload} to {topic}')
    resp = ipc_client.publish_to_iot_core(topic_name=topic, payload=payload)
    logger.info('Stopping...')
    ipc_client.close()


if __name__ == "__main__":
    main()
