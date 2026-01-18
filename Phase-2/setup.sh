# Black Hand OS - Buildroot Setup Script (PowerShell)
# Run this once to set up the development environment

Write-Host "=========================================="
Write-Host "  Black Hand OS - Development Setup"
Write-Host "=========================================="

# Create directory structure
Write-Host "[1/3] Creating directory structure..."
New-Item -ItemType Directory -Force -Path "custom" | Out-Null

# Build Docker image
Write-Host "[2/3] Building Docker image..."
docker-compose build

# Clone Buildroot inside the container
Write-Host "[3/3] Setting up Buildroot inside container..."
docker-compose run --rm buildroot bash -c 'if [ ! -d "/home/builder/buildroot/.git" ]; then echo "Cloning Buildroot..."; cd /home/builder; git clone https://gitlab.com/buildroot.org/buildroot.git buildroot; cd buildroot; LATEST_STABLE=$(git tag | grep "^2024" | sort -V | tail -1); echo "Checking out: $LATEST_STABLE"; git checkout "$LATEST_STABLE"; else echo "Buildroot already cloned."; fi'

Write-Host ""
Write-Host "=========================================="
Write-Host "  Setup Complete!"
Write-Host "=========================================="
Write-Host ""
Write-Host "  Next Steps:"
Write-Host "=========================================="
Write-Host ""
Write-Host "  1. Start the container:"
Write-Host "     docker-compose run --rm buildroot"
Write-Host ""
Write-Host "  2. Inside the container:"
Write-Host "     cd buildroot"
Write-Host "     make raspberrypi5_defconfig"
Write-Host "     make menuconfig"
Write-Host '     make -j$(nproc)'
Write-Host ""
Write-Host "  3. Copy image to Windows:"
Write-Host "     cp output/images/sdcard.img ~/custom/"
Write-Host ""
Write-Host "  4. Flash custom/sdcard.img using balenaEtcher"
Write-Host ""
Write-Host "=========================================="