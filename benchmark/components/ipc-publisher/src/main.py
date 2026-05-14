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
        PublishToTopicRequest,
        PublishMessage,
        BinaryMessage
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
logger = logging.getLogger('IPCPublisher')

class IPCPublisher:
    def __init__(self):
        self.config = self.load_configuration()
        self.ipc_client = None
        self.setup_ipc_client()
        
    def load_configuration(self):
        """Load component configuration"""
        try:
            config = {
                "topic": "local/sensor/data",
                "interval": 15,
                "messageType": "sensor-reading",
                "deviceId": "ipc-sensor-001"
            }
            
            # Load from environment variables
            config["topic"] = os.environ.get('GG_TOPIC', config["topic"])
            config["interval"] = int(os.environ.get('GG_INTERVAL', config["interval"]))
            config["messageType"] = os.environ.get('GG_MESSAGE_TYPE', config["messageType"])
            config["deviceId"] = os.environ.get('GG_DEVICE_ID', config["deviceId"])
            
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
    
    def generate_message_data(self):
        """Generate message data for IPC publishing"""
        data = {
            "messageType": self.config['messageType'],
            "deviceId": self.config['deviceId'],
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "sequenceNumber": int(time.time()),
            "data": {
                "temperature": round(random.uniform(18.0, 32.0), 2),
                "humidity": round(random.uniform(30.0, 80.0), 2),
                "pressure": round(random.uniform(980.0, 1020.0), 2)
            },
            "status": "active"
        }
        
        return data
    
    def publish_to_ipc(self, message_data):
        """Publish message via Greengrass IPC"""
        try:
            message_json = json.dumps(message_data)
            
            if self.ipc_client:
                # Real Greengrass IPC publishing
                request = PublishToTopicRequest()
                request.topic = self.config['topic']
                publish_message = PublishMessage()
                publish_message.binary_message = BinaryMessage()
                publish_message.binary_message.message = message_json.encode('utf-8')
                request.publish_message = publish_message
                
                operation = self.ipc_client.new_publish_to_topic()
                operation.activate(request)
                future = operation.get_response()
                future.result(timeout=10.0)
                
                logger.info(f"Published to IPC topic '{self.config['topic']}': {message_json}")
            else:
                # Simulation mode
                logger.info(f"[SIMULATION] Would publish to IPC topic '{self.config['topic']}': {message_json}")
                
        except Exception as e:
            logger.error(f"Failed to publish IPC message: {e}")
            raise
    
    def run(self):
        """Main component loop"""
        logger.info("IPC Publisher component starting...")
        logger.info(f"Configuration: {json.dumps(self.config, indent=2)}")
        
        try:
            while True:
                message_data = self.generate_message_data()
                self.publish_to_ipc(message_data)
                time.sleep(self.config['interval'])
                
        except KeyboardInterrupt:
            logger.info("IPC Publisher component stopping...")
        except Exception as e:
            logger.error(f"Unexpected error: {e}")
            sys.exit(1)
        finally:
            if self.ipc_client:
                self.ipc_client.close()

if __name__ == "__main__":
    publisher = IPCPublisher()
    publisher.run()
