# S3 Uploader Component

A Greengrass component that monitors a directory for new files and automatically uploads them to Amazon S3. Supports both real-time file monitoring and periodic polling.

## Features

- Real-time file monitoring using watchdog (with polling fallback)
- Configurable file patterns and size limits
- Optional file deletion after successful upload
- Metadata tagging with upload timestamp and device info
- Proper error handling and retry logic
- Simulation mode for local testing
- Requires Token Exchange Service for AWS credentials
- Universal runtime compatibility - works on both Greengrass and Lite

## Configuration

```json
{
  "watchDirectory": "/tmp/uploads",
  "s3Bucket": "my-greengrass-uploads",
  "s3Prefix": "device-uploads/",
  "uploadInterval": 60,
  "deleteAfterUpload": false,
  "filePattern": "*",
  "maxFileSize": 10485760
}
```

- `watchDirectory`: Local directory to monitor for files
- `s3Bucket`: Target S3 bucket name
- `s3Prefix`: S3 key prefix for uploaded files
- `uploadInterval`: Polling interval in seconds (fallback mode)
- `deleteAfterUpload`: Delete local files after successful upload
- `filePattern`: File pattern to match (e.g., "*.jpg", "data_*")
- `maxFileSize`: Maximum file size in bytes (default 10MB)

## Prerequisites

### S3 Bucket Policy

Your S3 bucket needs appropriate permissions. Example bucket policy:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Principal": {
        "AWS": "arn:aws:iam::ACCOUNT:role/GreengrassV2TokenExchangeRole"
      },
      "Action": [
        "s3:PutObject",
        "s3:PutObjectAcl"
      ],
      "Resource": "arn:aws:s3:::my-greengrass-uploads/*"
    }
  ]
}
```

### IAM Role Permissions

The Greengrass Token Exchange Service role needs:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "s3:PutObject",
        "s3:PutObjectAcl"
      ],
      "Resource": "arn:aws:s3:::my-greengrass-uploads/*"
    }
  ]
}
```

## Deployment Steps

### 1. Create S3 Bucket
```bash
aws s3 mb s3://my-greengrass-uploads
```

### 2. Prepare Artifacts
```bash
cd examples/s3-uploader
zip -r s3-uploader.zip src/
```

### 3. Upload to S3
```bash
aws s3 cp s3-uploader.zip s3://YOUR_BUCKET/s3-uploader/1.0.0/
```

### 4. Update Recipe
Edit `recipe.json` and replace:
- `YOUR_BUCKET` with your artifacts S3 bucket
- `my-greengrass-uploads` with your target bucket in configuration

### 5. Create Component
```bash
aws greengrassv2 create-component-version \
    --inline-recipe fileb://recipe.json
```

### 6. Deploy with TES
Ensure your deployment includes:
- `aws.greengrass.TokenExchangeService`
- `com.example.S3Uploader`

## Testing Locally

Test without Greengrass (simulation mode):

```bash
cd src
export GG_WATCH_DIR="/tmp/test-uploads"
export GG_S3_BUCKET="my-test-bucket"
export GG_UPLOAD_INTERVAL=10
mkdir -p /tmp/test-uploads
python3 main.py
```

In another terminal, add test files:
```bash
echo "test data" > /tmp/test-uploads/test.txt
```

## Usage Examples

### Upload Log Files
```json
{
  "watchDirectory": "/var/log/app",
  "s3Bucket": "my-logs-bucket",
  "s3Prefix": "device-logs/",
  "filePattern": "*.log",
  "deleteAfterUpload": true
}
```

### Upload Images
```json
{
  "watchDirectory": "/tmp/camera",
  "s3Bucket": "my-images-bucket", 
  "s3Prefix": "camera-images/",
  "filePattern": "*.jpg",
  "maxFileSize": 5242880
}
```

## Verification

### Check S3 Bucket
```bash
aws s3 ls s3://my-greengrass-uploads/device-uploads/
```

### Check Component Logs
```bash
sudo tail -f /greengrass/v2/logs/com.example.S3Uploader.log
```

## Troubleshooting

- **Files not uploading**: Check S3 permissions and TES configuration
- **Access denied errors**: Verify IAM role has S3 permissions
- **Large files failing**: Check maxFileSize configuration
- **Directory not found**: Ensure watchDirectory exists and is writable
- **Watchdog not working**: Component will fall back to polling mode automatically
