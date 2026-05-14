#!/usr/bin/env python3

import json
import logging
import os
import sys
import time
import fnmatch
from datetime import datetime
from pathlib import Path

try:
    import boto3
    from botocore.exceptions import ClientError, NoCredentialsError
    BOTO3_AVAILABLE = True
except ImportError:
    BOTO3_AVAILABLE = False
    logging.warning("boto3 not available - running in simulation mode")

try:
    from watchdog.observers import Observer
    from watchdog.events import FileSystemEventHandler
    WATCHDOG_AVAILABLE = True
except ImportError:
    WATCHDOG_AVAILABLE = False
    logging.warning("watchdog not available - using polling mode")

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('S3Uploader')

class FileUploadHandler(FileSystemEventHandler):
    """Handle file system events for immediate uploads"""
    
    def __init__(self, uploader):
        self.uploader = uploader
        
    def on_created(self, event):
        if not event.is_directory:
            logger.info(f"New file detected: {event.src_path}")
            # Small delay to ensure file is fully written
            time.sleep(1)
            self.uploader.upload_file(event.src_path)

class S3Uploader:
    def __init__(self):
        self.config = self.load_configuration()
        self.s3_client = None
        self.setup_s3_client()
        self.setup_watch_directory()
        
    def load_configuration(self):
        """Load component configuration"""
        try:
            config = {
                "watchDirectory": "/tmp/uploads",
                "s3Bucket": "my-greengrass-uploads", 
                "s3Prefix": "device-uploads/",
                "uploadInterval": 60,
                "deleteAfterUpload": False,
                "filePattern": "*",
                "maxFileSize": 10485760  # 10MB
            }
            
            # Load from environment variables
            config["watchDirectory"] = os.environ.get('GG_WATCH_DIR', config["watchDirectory"])
            config["s3Bucket"] = os.environ.get('GG_S3_BUCKET', config["s3Bucket"])
            config["s3Prefix"] = os.environ.get('GG_S3_PREFIX', config["s3Prefix"])
            config["uploadInterval"] = int(os.environ.get('GG_UPLOAD_INTERVAL', config["uploadInterval"]))
            config["deleteAfterUpload"] = os.environ.get('GG_DELETE_AFTER', 'false').lower() == 'true'
            config["filePattern"] = os.environ.get('GG_FILE_PATTERN', config["filePattern"])
            config["maxFileSize"] = int(os.environ.get('GG_MAX_FILE_SIZE', config["maxFileSize"]))
            
            return config
        except Exception as e:
            logger.error(f"Failed to load configuration: {e}")
            raise
    
    def setup_s3_client(self):
        """Initialize S3 client using Greengrass credentials"""
        if BOTO3_AVAILABLE:
            try:
                # In Greengrass, credentials are provided via TES
                self.s3_client = boto3.client('s3')
                logger.info("S3 client initialized")
            except Exception as e:
                logger.error(f"Failed to initialize S3 client: {e}")
                self.s3_client = None
        else:
            logger.info("Running in simulation mode - uploads will be logged only")
    
    def setup_watch_directory(self):
        """Create watch directory if it doesn't exist"""
        watch_dir = Path(self.config['watchDirectory'])
        try:
            watch_dir.mkdir(parents=True, exist_ok=True)
            logger.info(f"Watch directory ready: {watch_dir}")
        except Exception as e:
            logger.error(f"Failed to create watch directory: {e}")
            raise
    
    def should_upload_file(self, file_path):
        """Check if file should be uploaded based on configuration"""
        try:
            path = Path(file_path)
            
            # Check if file exists and is a regular file
            if not path.is_file():
                return False
            
            # Check file pattern
            if not fnmatch.fnmatch(path.name, self.config['filePattern']):
                logger.debug(f"File {path.name} doesn't match pattern {self.config['filePattern']}")
                return False
            
            # Check file size
            file_size = path.stat().st_size
            if file_size > self.config['maxFileSize']:
                logger.warning(f"File {path.name} too large ({file_size} bytes)")
                return False
            
            return True
            
        except Exception as e:
            logger.error(f"Error checking file {file_path}: {e}")
            return False
    
    def upload_file(self, file_path):
        """Upload a single file to S3"""
        try:
            if not self.should_upload_file(file_path):
                return False
            
            path = Path(file_path)
            s3_key = f"{self.config['s3Prefix']}{path.name}"
            
            if self.s3_client:
                # Real S3 upload
                self.s3_client.upload_file(
                    str(path),
                    self.config['s3Bucket'],
                    s3_key,
                    ExtraArgs={
                        'Metadata': {
                            'upload-timestamp': datetime.utcnow().isoformat(),
                            'source-device': os.environ.get('AWS_IOT_THING_NAME', 'unknown')
                        }
                    }
                )
                logger.info(f"Uploaded {path.name} to s3://{self.config['s3Bucket']}/{s3_key}")
            else:
                # Simulation mode
                logger.info(f"[SIMULATION] Would upload {path.name} to s3://{self.config['s3Bucket']}/{s3_key}")
            
            # Delete file after upload if configured
            if self.config['deleteAfterUpload']:
                path.unlink()
                logger.info(f"Deleted local file: {path.name}")
            
            return True
            
        except ClientError as e:
            logger.error(f"S3 upload failed for {file_path}: {e}")
            return False
        except Exception as e:
            logger.error(f"Unexpected error uploading {file_path}: {e}")
            return False
    
    def scan_and_upload_existing(self):
        """Scan watch directory and upload existing files"""
        try:
            watch_dir = Path(self.config['watchDirectory'])
            files_uploaded = 0
            
            for file_path in watch_dir.iterdir():
                if file_path.is_file():
                    if self.upload_file(file_path):
                        files_uploaded += 1
            
            if files_uploaded > 0:
                logger.info(f"Uploaded {files_uploaded} existing files")
                
        except Exception as e:
            logger.error(f"Error scanning directory: {e}")
    
    def run_with_watchdog(self):
        """Run with file system monitoring"""
        event_handler = FileUploadHandler(self)
        observer = Observer()
        observer.schedule(event_handler, self.config['watchDirectory'], recursive=False)
        observer.start()
        
        logger.info(f"Monitoring directory: {self.config['watchDirectory']}")
        
        try:
            # Also periodically scan for files (in case watchdog misses something)
            while True:
                time.sleep(self.config['uploadInterval'])
                self.scan_and_upload_existing()
        except KeyboardInterrupt:
            observer.stop()
        observer.join()
    
    def run_with_polling(self):
        """Run with periodic directory polling"""
        logger.info(f"Polling directory every {self.config['uploadInterval']} seconds")
        
        try:
            while True:
                self.scan_and_upload_existing()
                time.sleep(self.config['uploadInterval'])
        except KeyboardInterrupt:
            pass
    
    def run(self):
        """Main component loop"""
        logger.info("S3 Uploader component starting...")
        logger.info(f"Configuration: {json.dumps(self.config, indent=2)}")
        
        try:
            # Upload any existing files first
            self.scan_and_upload_existing()
            
            # Start monitoring
            if WATCHDOG_AVAILABLE:
                self.run_with_watchdog()
            else:
                self.run_with_polling()
                
        except KeyboardInterrupt:
            logger.info("S3 Uploader component stopping...")
        except Exception as e:
            logger.error(f"Unexpected error: {e}")
            sys.exit(1)

if __name__ == "__main__":
    uploader = S3Uploader()
    uploader.run()
