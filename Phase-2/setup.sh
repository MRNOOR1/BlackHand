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
echo "[1/5] Creating directory structure..."
mkdir -p buildroot output dl custom

# Clone Buildroot if not present
if [ ! -d "buildroot/.git" ]; then
    echo "[2/5] Cloning Buildroot..."
    git clone https://gitlab.com/buildroot.org/buildroot.git buildroot
    
    # Checkout stable release
    cd buildroot
    LATEST_STABLE=$(git tag | grep "^2024" | sort -V | tail -1)
    echo "      Checking out stable release: $LATEST_STABLE"
    git checkout "$LATEST_STABLE"
    cd ..
else
    echo "[2/5] Buildroot already cloned, skipping..."
fi

# Update docker-compose with correct user IDs
echo "[3/5] Configuring user permissions..."
USER_ID=$(id -u)
GROUP_ID=$(id -g)

# Update docker-compose.yml with actual user IDs
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS uses BSD sed
    sed -i '' "s/USER_ID: 1000/USER_ID: $USER_ID/" docker-compose.yml
    sed -i '' "s/GROUP_ID: 1000/GROUP_ID: $GROUP_ID/" docker-compose.yml
else
    # Linux uses GNU sed
    sed -i "s/USER_ID: 1000/USER_ID: $USER_ID/" docker-compose.yml
    sed -i "s/GROUP_ID: 1000/GROUP_ID: $GROUP_ID/" docker-compose.yml
fi

echo "      User ID: $USER_ID"
echo "      Group ID: $GROUP_ID"

# Build Docker image
echo "[4/5] Building Docker image..."
docker-compose build

echo "[5/5] Setup complete!"
echo ""
echo "=========================================="
echo "  Next Steps:"
echo "=========================================="
echo ""
echo "  1. Start the container:"
echo "     docker-compose run --rm buildroot"
echo ""
echo "  2. Inside the container, configure Buildroot:"
echo "     make raspberrypi5_defconfig"
echo "     make menuconfig"
echo ""
echo "  3. Build (takes 30-90 minutes first time):"
echo "     make -j\$(nproc)"
echo ""
echo "  4. Output image will be at:"
echo "     ./output/images/sdcard.img"
echo ""
echo "=========================================="