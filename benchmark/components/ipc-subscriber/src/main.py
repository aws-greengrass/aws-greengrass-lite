#!/usr/bin/env python3

import json
import logging
import os
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import awsiot.greengrasscoreipc
    from awsiot.greengrasscoreipc.model import (
        SubscribeToTopicRequest,
        SubscriptionResponseMessage
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
logger = logging.getLogger('IPCSubscriber')

class MessageHandler(awsiot.greengrasscoreipc.SubscribeToTopicStreamHandler):
    """Handle incoming IPC messages"""
    
    def __init__(self, subscriber):
        super().__init__()
        self.subscriber = subscriber
    
    def on_stream_event(self, event: SubscriptionResponseMessage) -> None:
        try:
            message = str(event.binary_message.message, 'utf-8')
            topic = event.topic_name if hasattr(event, 'topic_name') else 'unknown'
            
            logger.info(f"Received message on topic '{topic}': {message}")
            self.subscriber.process_message(topic, message)
            
        except Exception as e:
            logger.error(f"Error processing message: {e}")
    
    def on_stream_error(self, error: Exception) -> bool:
        logger.error(f"Stream error: {error}")
        return True  # Return True to keep the stream alive
    
    def on_stream_closed(self) -> None:
        logger.info("Message stream closed")

class IPCSubscriber:
    def __init__(self):
        self.config = self.load_configuration()
        self.ipc_client = None
        self.subscriptions = []
        self.setup_ipc_client()
        self.setup_output_file()
        
    def load_configuration(self):
        """Load component configuration"""
        try:
            config = {
                "topics": ["local/sensor/data", "local/alerts/*"],
                "processingMode": "log",
                "outputFile": "/tmp/ipc-messages.log"
            }
            
            # Load from environment variables
            topics_env = os.environ.get('GG_TOPICS')
            if topics_env:
                config["topics"] = topics_env.split(',')
            
            config["processingMode"] = os.environ.get('GG_PROCESSING_MODE', config["processingMode"])
            config["outputFile"] = os.environ.get('GG_OUTPUT_FILE', config["outputFile"])
            
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
            logger.info("Running in simulation mode")
    
    def setup_output_file(self):
        """Setup output file for message logging"""
        if self.config['processingMode'] == 'log' and self.config['outputFile']:
            try:
                output_path = Path(self.config['outputFile'])
                output_path.parent.mkdir(parents=True, exist_ok=True)
                logger.info(f"Output file configured: {output_path}")
            except Exception as e:
                logger.error(f"Failed to setup output file: {e}")
    
    def process_message(self, topic, message):
        """Process received IPC message"""
        try:
            # Parse message if it's JSON
            try:
                message_data = json.loads(message)
                formatted_message = json.dumps(message_data, indent=2)
            except json.JSONDecodeError:
                formatted_message = message
            
            # Log to file if configured
            if self.config['processingMode'] == 'log' and self.config['outputFile']:
                with open(self.config['outputFile'], 'a') as f:
                    timestamp = datetime.now().isoformat()
                    f.write(f"[{timestamp}] Topic: {topic}\n")
                    f.write(f"{formatted_message}\n")
                    f.write("-" * 50 + "\n")
            
            # Additional processing based on message type
            if isinstance(message_data, dict):
                msg_type = message_data.get('messageType', 'unknown')
                logger.info(f"Processing {msg_type} message from topic {topic}")
                
                # Example: Alert on high temperature
                if msg_type == 'sensor-reading':
                    data = message_data.get('data', {})
                    temp = data.get('temperature', 0)
                    if temp > 30:
                        logger.warning(f"High temperature alert: {temp}°C")
                        
        except Exception as e:
            logger.error(f"Error processing message: {e}")
    
    def subscribe_to_topics(self):
        """Subscribe to configured IPC topics"""
        if not self.ipc_client:
            logger.warning("No IPC client available - cannot subscribe")
            return
        
        for topic in self.config['topics']:
            try:
                request = SubscribeToTopicRequest()
                request.topic = topic
                
                handler = MessageHandler(self)
                operation = self.ipc_client.new_subscribe_to_topic(handler)
                future = operation.activate(request)
                future.result(timeout=10.0)
                
                self.subscriptions.append(operation)
                logger.info(f"Subscribed to IPC topic: {topic}")
                
            except Exception as e:
                logger.error(f"Failed to subscribe to topic {topic}: {e}")
    
    def run(self):
        """Main component loop"""
        logger.info("IPC Subscriber component starting...")
        logger.info(f"Configuration: {json.dumps(self.config, indent=2)}")
        
        try:
            if GREENGRASS_IPC_AVAILABLE and self.ipc_client:
                self.subscribe_to_topics()
                
                # Keep the component running
                logger.info("Listening for IPC messages...")
                while True:
                    time.sleep(10)
            else:
                # Simulation mode
                logger.info("Running in simulation mode - no actual subscriptions")
                while True:
                    logger.info("Would be listening for IPC messages...")
                    time.sleep(30)
                    
        except KeyboardInterrupt:
            logger.info("IPC Subscriber component stopping...")
        except Exception as e:
            logger.error(f"Unexpected error: {e}")
            sys.exit(1)
        finally:
            # Clean up subscriptions
            for subscription in self.subscriptions:
                try:
                    subscription.close()
                except:
                    pass
            if self.ipc_client:
                self.ipc_client.close()

if __name__ == "__main__":
    subscriber = IPCSubscriber()
    subscriber.run()
