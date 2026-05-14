# IPC Subscriber Component

A Greengrass component that subscribes to local IPC topics and processes messages from other components. Demonstrates inter-component communication patterns and message processing.

## Features

- Subscribes to multiple IPC topics (supports wildcards)
- Configurable message processing modes
- File logging of received messages
- JSON message parsing and formatting
- Alert processing based on message content
- Simulation mode for local testing
- No AWS credentials required (local communication only)
- Universal runtime compatibility - works on both Greengrass and Lite

## Configuration

```json
{
  "topics": ["local/sensor/data", "local/alerts/*"],
  "processingMode": "log",
  "outputFile": "/tmp/ipc-messages.log"
}
```

- `topics`: Array of IPC topics to subscribe to (supports wildcards)
- `processingMode`: How to process messages ("log", "process", etc.)
- `outputFile`: File path for logging received messages

## Message Processing

The component processes different message types:

- **sensor-reading**: Monitors for threshold alerts (e.g., high temperature)
- **alert**: Processes alert messages
- **status**: Handles status updates
- **custom**: Extensible for custom message types

## Deployment Steps

### 1. Prepare Artifacts
```bash
cd examples/ipc-subscriber
zip -r ipc-subscriber.zip src/
```

### 2. Upload to S3
```bash
aws s3 cp ipc-subscriber.zip s3://YOUR_BUCKET/ipc-subscriber/1.0.0/
```

### 3. Update Recipe
Edit `recipe.json` and replace `YOUR_BUCKET` with your S3 bucket name.

### 4. Create Component
```bash
aws greengrassv2 create-component-version \
    --inline-recipe fileb://recipe.json
```

### 5. Deploy to Device
Create a deployment with this component.

## Testing Locally

Test without Greengrass (simulation mode):

```bash
cd src
export GG_TOPICS="test/data,test/alerts/*"
export GG_OUTPUT_FILE="/tmp/test-messages.log"
python3 main.py
```

## Usage Examples

### Basic Sensor Monitoring
```json
{
  "topics": ["local/sensor/temperature", "local/sensor/humidity"],
  "processingMode": "log",
  "outputFile": "/var/log/sensor-data.log"
}
```

### Alert Processing
```json
{
  "topics": ["local/alerts/*", "local/warnings/*"],
  "processingMode": "process",
  "outputFile": "/var/log/alerts.log"
}
```

## Output Format

Messages are logged with timestamps and topic information:

```
[2024-01-01T12:00:00.000000] Topic: local/sensor/data
{
  "messageType": "sensor-reading",
  "deviceId": "sensor-001",
  "timestamp": "2024-01-01T12:00:00.000Z",
  "data": {
    "temperature": 31.5,
    "humidity": 65.2
  }
}
--------------------------------------------------
```

## Verification

### Check Component Logs
```bash
sudo tail -f /greengrass/v2/logs/com.example.IPCSubscriber.log
```

### Check Message Log File
```bash
tail -f /tmp/ipc-messages.log
```

### Test with Publisher
Deploy alongside the IPC Publisher component to see message flow.

## Extending the Component

Add custom message processing by modifying the `process_message` method:

```python
def process_message(self, topic, message):
    message_data = json.loads(message)
    
    # Custom processing logic
    if message_data.get('messageType') == 'custom-alert':
        self.handle_custom_alert(message_data)
```

## Troubleshooting

- **No messages received**: Check topic names and ensure publisher is running
- **Subscription failed**: Verify Greengrass IPC permissions
- **File write errors**: Check output file path permissions
- **Memory usage**: Limit retained message history for long-running deployments
