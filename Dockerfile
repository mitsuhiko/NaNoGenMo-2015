FROM --platform=linux/amd64 ubuntu:18.04

# Disable interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install necessary packages including 32-bit support
RUN apt-get update && apt-get install -y \
    gcc \
    gcc-multilib \
    make \
    libc6-dev \
    libc6-dev-i386 \
    linux-libc-dev \
    && rm -rf /var/lib/apt/lists/*

# Create working directory
WORKDIR /app

# Copy source files
COPY C/simple_test.c ./test.c
COPY C/msdos.c ./msdos.c
COPY RACTER/ /tmp/racter/

# List files to verify they're copied
RUN ls -la /tmp/racter/

# First test if vm86 is available
RUN gcc -o test test.c

# Check what headers are available
RUN find /usr/include -name "*vm86*" || echo "No vm86 headers found"
RUN ls -la /usr/include/sys/ | grep vm || echo "No vm86 in /usr/include/sys/"

# Create a simple Makefile that compiles for 32-bit
RUN echo 'msdos: msdos.c' > Makefile && \
    echo '\tgcc -m32 -o msdos msdos.c' >> Makefile

# Build the emulator
RUN make msdos

# Copy test files for testing
COPY C/tests/run_tests.sh /app/

# Change to the racter directory before running
WORKDIR /tmp/racter

# Default command - should show the proper Racter greeting  
CMD ["/app/msdos", "RACTER.EXE"]