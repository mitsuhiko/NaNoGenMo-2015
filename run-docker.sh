#!/bin/bash

# Build Docker image for x86 platform
echo "Building Docker image for x86 platform..."
docker build --platform=linux/amd64 -t racter-dos .

# Run the container with proper privileges for vm86
echo "Running Racter in Docker..."
docker run --rm -it --privileged --platform=linux/amd64 racter-dos