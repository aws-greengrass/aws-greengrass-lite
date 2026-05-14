# IoT Core Publisher Component

A Greengrass component that generates simulated sensor data and publishes it to AWS IoT Core topics. Demonstrates proper use of Greengrass IPC for IoT Core communication.

## Features

- Generates realistic sensor data with configurable ranges
- Publishes to configurable IoT Core topics
- Supports QoS 0 and 1 messaging
- Proper error handling and retry logic
- Simulation mode for local testing
- Requires Token Exchange Service for AWS credentials
- Universal runtime compatibility - works on both Greengrass and Lite

## Configuration

```json
{
  "topic": "sensor/data",
  "interval": 30,
  "deviceId": "sensor-001", 
  "sensorType": "temperature",
  "minValue": 20.0,
  "maxValue": 30.0,
  "qos": 1
}
```

- `topic`: IoT Core topic to publish to
- `interval`: Publishing interval in seconds
- `deviceId`: Unique identifier for this sensor
- `sensorType`: Type of sensor (affects units)
- `minValue`/`maxValue`: Range for simulated values
- `qos`: Quality of Service (0 or 1)

## Message Format

Published messages follow this structure:

```json
{
  "deviceId": "sensor-001",
  "sensorType": "temperature", 
  "value": 24.5,
  "unit": "°C",
  "timestamp": "2024-01-01T12:00:00.000Z",
  "quality": "good"
}
```

## Prerequisites

### IoT Policy Requirements

Your Greengrass device needs an IoT policy allowing:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "iot:Publish"
      ],
      "Resource": [
        "arn:aws:iot:*:*:topic/sensor/*"
      ]
    }
  ]
}
```

### Token Exchange Service

This component requires the Token Exchange Service (TES) to be deployed and configured. TES provides AWS credentials for IoT Core access.

## Deployment Steps

### 1. Install Dependencies
```bash
# The component requires the Greengrass IPC SDK
pip3 install awsiotsdk
```

### 2. Prepare Artifacts
```bash
cd examples/iot-core-publisher
zip -r iot-core-publisher.zip src/
```

### 3. Upload to S3
```bash
aws s3 cp iot-core-publisher.zip s3://YOUR_BUCKET/iot-core-publisher/1.0.0/
```

### 4. Update Recipe
Edit `recipe.json` and replace `YOUR_BUCKET` with your S3 bucket name.

### 5. Create Component
```bash
aws greengrassv2 create-component-version \
    --inline-recipe fileb://recipe.json
```

### 6. Deploy with TES
Ensure your deployment includes:
- `aws.greengrass.TokenExchangeService`
- `com.example.IoTCorePublisher`

## Testing Locally

Test without Greengrass (simulation mode):

```bash
cd src
export GG_TOPIC="test/sensor"
export GG_INTERVAL=5
export GG_DEVICE_ID="test-sensor"
python3 main.py
```

## Verification

### Check IoT Core
Monitor your IoT Core topic in the AWS Console:
1. Go to IoT Core → Test → MQTT test client
2. Subscribe to your topic (e.g., `sensor/data`)
3. Verify messages are arriving

### Check Greengrass Logs
```bash
# On the Greengrass device
sudo tail -f /greengrass/v2/logs/com.example.IoTCorePublisher.log
```

## Troubleshooting

- **No messages in IoT Core**: Check IoT policy permissions and TES configuration
- **IPC connection failed**: Verify Greengrass is running and component has proper permissions
- **Authentication errors**: Ensure Token Exchange Service is deployed and role has IoT permissions
- **Topic not found**: Verify topic name matches IoT policy resources
