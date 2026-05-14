#!/usr/bin/env python3

import json
import logging
import os
import sys
import time
from pathlib import Path

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('HelloWorld')

class HelloWorldComponent:
    def __init__(self):
        self.config = self.load_configuration()
        self.setup_logging()
        
    def load_configuration(self):
        """Load component configuration from Greengrass"""
        try:
            # Default configuration
            config = {
                "message": "Hello from Greengrass!",
                "interval": 10,
                "logLevel": "INFO"
            }
            
            # Try to load from Greengrass configuration
            config_path = os.environ.get('AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT')
            if config_path:
                # In a real deployment, this would use the Greengrass IPC client
                # For now, we'll use environment variables or default config
                config["message"] = os.environ.get('GG_MESSAGE', config["message"])
                config["interval"] = int(os.environ.get('GG_INTERVAL', config["interval"]))
                config["logLevel"] = os.environ.get('GG_LOG_LEVEL', config["logLevel"])
            
            return config
        except Exception as e:
            logger.error(f"Failed to load configuration: {e}")
            return {
                "message": "Hello from Greengrass!",
                "interval": 10,
                "logLevel": "INFO"
            }
    
    def setup_logging(self):
        """Configure logging based on component configuration"""
        log_level = getattr(logging, self.config.get('logLevel', 'INFO').upper())
        logger.setLevel(log_level)
        
    def run(self):
        """Main component loop"""
        logger.info("HelloWorld component starting...")
        logger.info(f"Configuration: {json.dumps(self.config, indent=2)}")
        
        try:
            while True:
                logger.info(self.config['message'])
                time.sleep(self.config['interval'])
                
        except KeyboardInterrupt:
            logger.info("HelloWorld component stopping...")
        except Exception as e:
            logger.error(f"Unexpected error: {e}")
            sys.exit(1)

if __name__ == "__main__":
    component = HelloWorldComponent()
    component.run()
