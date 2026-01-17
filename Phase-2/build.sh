#!/bin/bash

# Black Hand OS - Build Helper
# Convenience script for common operations

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

show_help() {
    echo "Black Hand OS - Build Helper"
    echo ""
    echo "Usage: ./build.sh <command>"
    echo ""
    echo "Commands:"
    echo "  shell       - Enter the build container interactively"
    echo "  menuconfig  - Run Buildroot menuconfig"
    echo "  build       - Run full build"
    echo "  rebuild     - Clean and rebuild"
    echo "  clean       - Clean build artifacts (keep config)"
    echo "  distclean   - Full clean (removes config too)"
    echo "  flash       - Show flash instructions"
    echo "  status      - Show build status"
    echo "  copy        - Copy sdcard.img to Windows"
    echo ""
}

enter_shell() {
    echo "Entering build container..."
    echo "Once inside, run: cd buildroot"
    docker-compose run --rm buildroot
}

run_menuconfig() {
    docker-compose run --rm buildroot bash -c 'cd buildroot && make menuconfig'
}

run_build() {
    echo "Starting build..."
    docker-compose run --rm buildroot bash -c '
        set -e
        cd /home/builder/buildroot
        
        # Check if configured
        if [ ! -f ".config" ]; then
            echo "No .config found. Run menuconfig first:"
            echo "  ./build.sh shell"
            echo "  cd buildroot"
            echo "  make raspberrypi5_defconfig"
            echo "  make menuconfig"
            exit 1
        fi
        
        # Build
        make -j$(nproc)
        
        # Copy result to Windows-accessible folder
        cp output/images/sdcard.img /home/builder/custom/
        
        echo ""
        echo "=========================================="
        echo "  Build complete!"
        echo "  Image copied to: custom/sdcard.img"
        echo "=========================================="
    '
}

run_clean() {
    docker-compose run --rm buildroot bash -c 'cd buildroot && make clean'
}

run_distclean() {
    docker-compose run --rm buildroot bash -c 'cd buildroot && make distclean'
}

copy_image() {
    docker-compose run --rm buildroot bash -c '
        if [ -f "/home/builder/buildroot/output/images/sdcard.img" ]; then
            cp /home/builder/buildroot/output/images/sdcard.img /home/builder/custom/
            echo "Image copied to: custom/sdcard.img"
        else
            echo "No image found. Run build first."
        fi
    '
}

show_flash_instructions() {
    echo "=========================================="
    echo "  Flash Instructions"
    echo "=========================================="
    echo ""
    echo "  Image location:"
    echo "    custom/sdcard.img"
    echo ""
    echo "  On Windows:"
    echo "    1. Download balenaEtcher from https://etcher.balena.io/"
    echo "    2. Open balenaEtcher"
    echo "    3. Select custom/sdcard.img"
    echo "    4. Select your SD card"
    echo "    5. Click Flash"
    echo ""
    echo "  WARNING: Double-check you selected the SD card!"
    echo "=========================================="
}

show_status() {
    docker-compose run --rm buildroot bash -c '
        echo "=========================================="
        echo "  Build Status"
        echo "=========================================="
        
        if [ -f "/home/builder/buildroot/.config" ]; then
            echo "  Config: EXISTS"
        else
            echo "  Config: NOT FOUND (run defconfig + menuconfig)"
        fi
        
        echo ""
        
        if [ -f "/home/builder/buildroot/output/images/sdcard.img" ]; then
            SIZE=$(du -h /home/builder/buildroot/output/images/sdcard.img | cut -f1)
            echo "  Image: EXISTS ($SIZE)"
        else
            echo "  Image: NOT BUILT"
        fi
        
        echo ""
        
        if [ -f "/home/builder/custom/sdcard.img" ]; then
            SIZE=$(du -h /home/builder/custom/sdcard.img | cut -f1)
            echo "  Copied to Windows: YES ($SIZE)"
        else
            echo "  Copied to Windows: NO"
        fi
        
        echo "=========================================="
    '
}

# Main
case "${1:-help}" in
    shell)
        enter_shell
        ;;
    menuconfig)
        run_menuconfig
        ;;
    build)
        run_build
        ;;
    rebuild)
        run_clean
        run_build
        ;;
    clean)
        run_clean
        ;;
    distclean)
        run_distclean
        ;;
    flash)
        show_flash_instructions
        ;;
    status)
        show_status
        ;;
    copy)
        copy_image
        ;;
    *)
        show_help
        ;;
esac