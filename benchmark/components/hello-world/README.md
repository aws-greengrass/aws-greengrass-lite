# Hello World Component

A simple Greengrass component that logs configurable messages at regular intervals. Perfect for testing basic component deployment and configuration.

## Features

- Configurable message content
- Adjustable logging interval
- Configurable log level
- Proper error handling and graceful shutdown
- Universal runtime compatibility (`"runtime": "*"`) - works on both Greengrass and Lite

## Configuration

The component accepts the following configuration parameters:

```json
{
  "message": "Hello from Greengrass!",
  "interval": 10,
  "logLevel": "INFO"
}
```

- `message`: The message to log (string)
- `interval`: Time between messages in seconds (integer)
- `logLevel`: Logging level (DEBUG, INFO, WARN, ERROR)

## Deployment Steps

### 1. Prepare Artifacts
```bash
# Create deployment package
cd examples/hello-world
zip -r hello-world.zip src/
```

### 2. Upload to S3
```bash
# Upload to your S3 bucket
aws s3 cp hello-world.zip s3://YOUR_BUCKET/hello-world/1.0.0/
```

### 3. Update Recipe
Edit `recipe.json` and replace `YOUR_BUCKET` with your actual S3 bucket name.

### 4. Create Component
```bash
# Create the component in AWS IoT Greengrass
aws greengrassv2 create-component-version \
    --inline-recipe fileb://recipe.json
```

### 5. Deploy to Device
Create a deployment targeting your Greengrass core device with this component.

## Testing Locally

You can test the component locally before deployment:

```bash
cd src
python3 main.py
```

Set environment variables to test different configurations:
```bash
export GG_MESSAGE="Testing locally!"
export GG_INTERVAL=5
export GG_LOG_LEVEL=DEBUG
python3 main.py
```

## Expected Output

```
2024-01-01 12:00:00,000 - HelloWorld - INFO - HelloWorld component starting...
2024-01-01 12:00:00,001 - HelloWorld - INFO - Configuration: {
  "message": "Hello from Greengrass!",
  "interval": 10,
  "logLevel": "INFO"
}
2024-01-01 12:00:00,001 - HelloWorld - INFO - Hello from Greengrass!
2024-01-01 12:00:10,002 - HelloWorld - INFO - Hello from Greengrass!
```

## Troubleshooting

- **Component not starting**: Check Greengrass logs for Python path issues
- **Configuration not loading**: Verify component configuration in deployment
- **Permission errors**: Ensure component has proper file system permissions
