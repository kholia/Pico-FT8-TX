# Based on Ubuntu latest LTS

FROM ubuntu:latest

# Build argument for serial number binding (optional)
ARG AUTHORIZED_SERIAL=""

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Set the Pico board type (can be overridden by build script)
ENV PICO_BOARD=pico2_w

# Set PICO_SDK_PATH to the location where it will be in the container
ENV PICO_SDK_PATH=/workspace/pico-sdk

# Install required dependencies for Pico SDK and CMake builds
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    git \
    python3 \
    python3-pip \
    pkg-config \
    libusb-1.0-0-dev \
    && rm -rf /var/lib/apt/lists/*

# Create workspace directory
WORKDIR /workspace

# Clone the Pico SDK from the develop branch
RUN git clone --branch develop https://github.com/raspberrypi/pico-sdk.git /workspace/pico-sdk && \
    cd /workspace/pico-sdk && \
    git submodule update --init --recursive

RUN cd /workspace/pico-sdk && \
    git pull && \
    cd lib/tinyusb && \
    git checkout master && git pull

# Trust only previously tested commits!
RUN cd /workspace/pico-sdk && \
    git checkout 8fcd44a1718337861214ba5499a8faceea2bfa1d && \
    cd lib/tinyusb && \
    git checkout 97741a56e0d8796e46ebb1268274853ba8eea847

# Build and install picotool
RUN cd /workspace && \
    git clone https://github.com/raspberrypi/picotool.git && \
    cd picotool && \
    mkdir build && \
    cd build && \
    cmake .. -DPICO_SDK_PATH=/workspace/pico-sdk && \
    make -j$(nproc) && \
    make install

ADD "https://www.random.org/cgi-bin/randbyte?nbytes=10&format=h" skipcache

# Copy the firmware source code
COPY . /workspace/

# Default command - show build artifacts and available tools
CMD ["sh", "-c", "echo 'Build complete. Firmware artifacts:' && ls -lh *.uf2 *.elf *.bin 2>/dev/null || echo 'No firmware files found in root directory' && echo && echo 'Available tools: picotool' && picotool version"]
