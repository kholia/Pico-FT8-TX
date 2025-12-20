#!/usr/bin/env bash

echo "Building Pico FT8 firmware with Docker..."

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    echo "Error: Docker is not installed or not in PATH"
    echo "Please install Docker and try again"
    exit 1
fi

# Build the Docker image
echo "Building Docker image..."
docker build -t pico-ft8-build .

# Run the container to build the firmware
echo "Building firmware in Docker container..."
docker run --rm \
  -v "$(pwd)":/host \
  -e PICO_SDK_PATH=/workspace/pico-sdk \
  pico-ft8-build \
  sh -c "
    echo 'Setting up build environment...' &&
    cd /workspace &&
    export PICO_BOARD=pico2_w &&
    echo 'Running cmake...' &&
    cmake . &&
    echo 'Building with make...' &&
    make -j$(nproc) &&
    echo 'Build complete. Copying artifacts...' &&
    cp *.uf2 *.elf *.bin *.hex /host/ 2>/dev/null || true &&
    echo 'Firmware built successfully!' &&
    ls -lh *.uf2 *.elf *.bin *.hex 2>/dev/null || echo 'No firmware files found'
  "

# Check if the UF2 file was created
if [ -f "pico-wspr-tx.uf2" ]; then
    echo ""
    echo "✅ Build successful! Firmware ready:"
    ls -lh pico-wspr-tx.uf2
    echo ""
    echo "To flash the firmware, run:"
    echo "picotool load -f pico-wspr-tx.uf2"
    echo ""
    echo "Or run the automated flash command:"
    echo "./picoload.sh"
else
    echo "❌ Build failed! UF2 file not found."
    exit 1
fi
