# IPC Publisher Component

A Greengrass component that publishes messages to local IPC topics for inter-component communication. Useful for creating data pipelines between components on the same device.

## Features

- Publishes to configurable local IPC topics
- Generates simulated sensor data with multiple metrics
- Configurable publishing intervals
- Proper error handling and connection management
- Simulation mode for local testing
- No AWS credentials required (local communication only)
- Universal runtime compatibility - works on both Greengrass and Lite

## Configuration

```json
{
  "topic": "local/sensor/data",
  "interval": 15,
  "messageType": "sensor-reading",
  "deviceId": "ipc-sensor-001"
}
```

- `topic`: Local IPC topic to publish to
- `interval`: Publishing interval in seconds
- `messageType`: Type identifier for messages
- `deviceId`: Unique identifier for this publisher

## Message Format

Published messages follow this structure:

```json
{
  "messageType": "sensor-reading",
  "deviceId": "ipc-sensor-001",
  "timestamp": "2024-01-01T12:00:00.000Z",
  "sequenceNumber": 1704110400,
  "data": {
    "temperature": 24.5,
    "humidity": 65.2,
    "pressure": 1013.2
  },
  "status": "active"
}
```

## Deployment Steps

### 1. Prepare Artifacts
```bash
cd examples/ipc-publisher
zip -r ipc-publisher.zip src/
```

### 2. Upload to S3
```bash
aws s3 cp ipc-publisher.zip s3://YOUR_BUCKET/ipc-publisher/1.0.0/
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
export GG_TOPIC="test/local/data"
export GG_INTERVAL=5
export GG_DEVICE_ID="test-publisher"
python3 main.py
```

## Usage with Subscriber

Deploy alongside the IPC Subscriber component to see end-to-end communication:

1. Deploy both `com.example.IPCPublisher` and `com.example.IPCSubscriber`
2. Configure matching topics
3. Check subscriber logs to see received messages

## Verification

### Check Component Logs
```bash
sudo tail -f /greengrass/v2/logs/com.example.IPCPublisher.log
```

### Monitor IPC Traffic
Use the IPC Subscriber component or check Greengrass nucleus logs for IPC activity.

## Troubleshooting

- **IPC connection failed**: Verify Greengrass is running and component has proper permissions
- **Messages not received**: Check topic names match between publisher and subscriber
- **High CPU usage**: Increase interval to reduce publishing frequency
