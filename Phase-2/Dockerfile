# Buildroot Development Environment
# Ubuntu 22.04 LTS - stable, well-supported

FROM ubuntu:22.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install Buildroot dependencies
RUN apt-get update && apt-get install -y \
    # Core build tools
    build-essential \
    gcc \
    g++ \
    make \
    # Version control
    git \
    # Download tools
    wget \
    curl \
    # Archive tools
    cpio \
    unzip \
    rsync \
    # Buildroot specific requirements
    bc \
    libncurses5-dev \
    libncursesw5-dev \
    # File utilities
    file \
    # Python (some packages need it)
    python3 \
    python3-pip \
    # Disk image tools
    dosfstools \
    mtools \
    # Device tree compiler
    device-tree-compiler \
    # For menuconfig
    libssl-dev \
    # Useful utilities
    nano \
    vim \
    tree \
    # Clean up apt cache to reduce image size
    && rm -rf /var/lib/apt/lists/*

# Create non-root user (Buildroot doesn't like running as root)
ARG USER_ID=1000
ARG GROUP_ID=1000

RUN groupadd -g ${GROUP_ID} builder && \
    useradd -m -u ${USER_ID} -g builder builder

# Set up working directory
WORKDIR /home/builder/buildroot

# Switch to non-root user
USER builder

# Default command
CMD ["/bin/bash"]