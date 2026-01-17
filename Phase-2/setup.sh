#!/bin/bash

# Black Hand OS - Buildroot Setup Script
# Run this once to set up the development environment

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=========================================="
echo "  Black Hand OS - Development Setup"
echo "=========================================="

# Create directory structure
echo "[1/3] Creating directory structure..."
mkdir -p custom

# Build Docker image
echo "[2/3] Building Docker image..."
docker-compose build

# Clone Buildroot inside the container (avoids Windows filesystem issues)
echo "[3/3] Setting up Buildroot inside container..."
docker-compose run --rm buildroot bash -c '
    if [ ! -d "/home/builder/buildroot/.git" ]; then
        echo "Cloning Buildroot..."
        cd /home/builder
        git clone https://gitlab.com/buildroot.org/buildroot.git buildroot
        cd buildroot
        LATEST_STABLE=$(git tag | grep "^2024" | sort -V | tail -1)
        echo "Checking out stable release: $LATEST_STABLE"
        git checkout "$LATEST_STABLE"
    else
        echo "Buildroot already cloned."
    fi
'

echo ""
echo "=========================================="
echo "  Setup Complete!"
echo "=========================================="
echo ""
echo "  Next Steps:"
echo "=========================================="
echo ""
echo "  1. Start the container:"
echo "     docker-compose run --rm buildroot"
echo ""
echo "  2. Inside the container:"
echo "     cd buildroot"
echo "     make raspberrypi5_defconfig"
echo "     make menuconfig"
echo "     make -j\$(nproc)"
echo ""
echo "  3. Copy image to Windows:"
echo "     cp output/images/sdcard.img ~/custom/"
echo ""
echo "  4. Flash custom/sdcard.img using balenaEtcher"
echo ""
echo "=========================================="