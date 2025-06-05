#!/bin/bash
# Docker test runner for DOS emulator

set -e

echo "Building Docker image for testing..."
cd ../../
docker build -t nanogenmo-test .

echo "Running tests in Docker container..."
docker run --rm nanogenmo-test /bin/bash -c "
cd /app && 
cp C/tests/run_tests.sh ./
chmod +x run_tests.sh &&
./run_tests.sh
"

echo "Docker tests complete!"