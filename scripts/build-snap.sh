#!/bin/bash
set -e

# Build snap package locally for testing
echo "Building AWS Greengrass Lite snap package..."

# Check if snapcraft is installed
if ! command -v snapcraft &> /dev/null; then
    echo "snapcraft is not installed. Installing..."
    sudo snap install snapcraft --classic
fi

# Clean previous builds
snapcraft clean

# Build the snap
snapcraft

echo "Snap package built successfully!"
echo "To install locally: sudo snap install --dangerous *.snap"
echo "To test: sudo snap connect aws-greengrass-lite:hardware-observe"
echo "         sudo snap connect aws-greengrass-lite:system-observe"
echo "         sudo snap connect aws-greengrass-lite:mount-observe"
echo "         sudo snap connect aws-greengrass-lite:process-control"
echo "         sudo snap connect aws-greengrass-lite:log-observe"
