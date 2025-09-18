# AWS Greengrass Lite Snap Package

This document describes how to install and use AWS Greengrass Lite as a Snap package.

## Installation

### From Snap Store

```bash
sudo snap install aws-greengrass-lite
```

### From Local Build

```bash
# Build the snap package
./scripts/build-snap.sh

# Install the locally built snap
sudo snap install --dangerous *.snap
```

## Configuration

After installation, you need to connect the required interfaces:

```bash
sudo snap connect aws-greengrass-lite:hardware-observe
sudo snap connect aws-greengrass-lite:system-observe
sudo snap connect aws-greengrass-lite:mount-observe
sudo snap connect aws-greengrass-lite:process-control
sudo snap connect aws-greengrass-lite:log-observe
```

## Usage

The snap package runs as a daemon service. You can control it using standard snap commands:

```bash
# Start the service
sudo snap start aws-greengrass-lite

# Stop the service
sudo snap stop aws-greengrass-lite

# Restart the service
sudo snap restart aws-greengrass-lite

# Check service status
sudo snap services aws-greengrass-lite

# View logs
sudo snap logs aws-greengrass-lite
```

## Configuration Files

Configuration files are stored in:
- `/var/snap/aws-greengrass-lite/current/etc/greengrass/`
- `/var/snap/aws-greengrass-lite/current/var/lib/greengrass/`

## Troubleshooting

### Permission Issues

If you encounter permission issues, ensure all required interfaces are connected:

```bash
snap connections aws-greengrass-lite
```

### Logs

View detailed logs:

```bash
sudo snap logs aws-greengrass-lite -f
```

## Building from Source

To build the snap package locally:

1. Install snapcraft:
   ```bash
   sudo snap install snapcraft --classic
   ```

2. Build the package:
   ```bash
   ./scripts/build-snap.sh
   ```

## Publishing

The snap package is automatically built and published through GitHub Actions:
- Commits to `main` branch are published to the `edge` channel
- Tagged releases are published to the `stable` channel
