#!/usr/bin/env python3

import json
import logging
import os
import random
import sys
import time
from datetime import datetime, timezone

try:
    import awsiot.greengrasscoreipc
    from awsiot.greengrasscoreipc.model import (
        IoTCoreMessage,
        PublishToIoTCoreRequest,
        QOS
    )
    GREENGRASS_IPC_AVAILABLE = True
except ImportError:
    GREENGRASS_IPC_AVAILABLE = False
    logging.warning("Greengrass IPC not available - running in simulation mode")

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('IoTCorePublisher')

class IoTCorePublisher:
    def __init__(self):
        self.config = self.load_configuration()
        self.ipc_client = None
        self.setup_ipc_client()
        
    def load_configuration(self):
        """Load component configuration"""
        try:
            config = {
                "topic": "sensor/data",
                "interval": 30,
                "deviceId": "sensor-001",
                "sensorType": "temperature",
                "minValue": 20.0,
                "maxValue": 30.0,
                "qos": 1
            }
            
            # Load from environment variables (for testing)
            config["topic"] = os.environ.get('GG_TOPIC', config["topic"])
            config["interval"] = int(os.environ.get('GG_INTERVAL', config["interval"]))
            config["deviceId"] = os.environ.get('GG_DEVICE_ID', config["deviceId"])
            config["sensorType"] = os.environ.get('GG_SENSOR_TYPE', config["sensorType"])
            config["minValue"] = float(os.environ.get('GG_MIN_VALUE', config["minValue"]))
            config["maxValue"] = float(os.environ.get('GG_MAX_VALUE', config["maxValue"]))
            config["qos"] = int(os.environ.get('GG_QOS', config["qos"]))
            
            return config
        except Exception as e:
            logger.error(f"Failed to load configuration: {e}")
            raise
    
    def setup_ipc_client(self):
        """Initialize Greengrass IPC client"""
        if GREENGRASS_IPC_AVAILABLE:
            try:
                self.ipc_client = awsiot.greengrasscoreipc.connect()
                logger.info("Connected to Greengrass IPC")
            except Exception as e:
                logger.error(f"Failed to connect to Greengrass IPC: {e}")
                self.ipc_client = None
        else:
            logger.info("Running in simulation mode - messages will be logged only")
    
    def generate_sensor_data(self):
        """Generate simulated sensor data"""
        value = random.uniform(self.config['minValue'], self.config['maxValue'])
        
        data = {
            "deviceId": self.config['deviceId'],
            "sensorType": self.config['sensorType'],
            "value": round(value, 2),
            "unit": "°C" if self.config['sensorType'] == "temperature" else "units",
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "quality": "good"
        }
        
        return data
    
    def publish_to_iot_core(self, message_data):
        """Publish message to IoT Core"""
        try:
            message_json = json.dumps(message_data)
            
            if self.ipc_client:
                # Real Greengrass deployment
                qos_map = {0: QOS.AT_MOST_ONCE, 1: QOS.AT_LEAST_ONCE}
                qos = qos_map.get(self.config['qos'], QOS.AT_LEAST_ONCE)
                
                request = PublishToIoTCoreRequest()
                request.topic_name = self.config['topic']
                request.payload = message_json.encode('utf-8')
                request.qos = qos
                
                operation = self.ipc_client.new_publish_to_iot_core()
                operation.activate(request)
                future = operation.get_response()
                future.result(timeout=10.0)
                
                logger.info(f"Published to IoT Core topic '{self.config['topic']}': {message_json}")
            else:
                # Simulation mode
                logger.info(f"[SIMULATION] Would publish to topic '{self.config['topic']}': {message_json}")
                
        except Exception as e:
            logger.error(f"Failed to publish message: {e}")
            raise
    
    def run(self):
        """Main component loop"""
        logger.info("IoT Core Publisher component starting...")
        logger.info(f"Configuration: {json.dumps(self.config, indent=2)}")
        
        try:
            while True:
                sensor_data = self.generate_sensor_data()
                self.publish_to_iot_core(sensor_data)
                time.sleep(self.config['interval'])
                
        except KeyboardInterrupt:
            logger.info("IoT Core Publisher component stopping...")
        except Exception as e:
            logger.error(f"Unexpected error: {e}")
            sys.exit(1)
        finally:
            if self.ipc_client:
                self.ipc_client.close()

if __name__ == "__main__":
    publisher = IoTCorePublisher()
    publisher.run()
