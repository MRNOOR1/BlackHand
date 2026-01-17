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
    echo ""
}

enter_shell() {
    echo "Entering build container..."
    docker-compose run --rm buildroot
}

run_menuconfig() {
    docker-compose run --rm buildroot make menuconfig
}

run_build() {
    echo "Starting build (this may take 30-90 minutes)..."
    docker-compose run --rm buildroot make -j$(nproc)
    echo ""
    echo "Build complete! Image at: ./output/images/sdcard.img"
}

run_clean() {
    docker-compose run --rm buildroot make clean
}

run_distclean() {
    docker-compose run --rm buildroot make distclean
}

show_flash_instructions() {
    echo "=========================================="
    echo "  Flash Instructions"
    echo "=========================================="
    echo ""
    echo "  1. Find your SD card device:"
    echo "     Linux:  lsblk"
    echo "     macOS:  diskutil list"
    echo ""
    echo "  2. Unmount the SD card:"
    echo "     Linux:  sudo umount /dev/sdX*"
    echo "     macOS:  diskutil unmountDisk /dev/diskN"
    echo ""
    echo "  3. Flash the image:"
    echo "     Linux:  sudo dd if=./output/images/sdcard.img of=/dev/sdX bs=4M status=progress"
    echo "     macOS:  sudo dd if=./output/images/sdcard.img of=/dev/rdiskN bs=4m"
    echo ""
    echo "  4. Sync and eject:"
    echo "     sudo sync"
    echo ""
    echo "  WARNING: Double-check the device path!"
    echo "  Wrong path = data loss on wrong drive!"
    echo "=========================================="
}

show_status() {
    echo "=========================================="
    echo "  Build Status"
    echo "=========================================="
    
    if [ -f "buildroot/.config" ]; then
        echo "  Config: EXISTS"
        # Extract some info from config
        grep "BR2_DEFCONFIG=" buildroot/.config 2>/dev/null || echo "  Target: Custom config"
    else
        echo "  Config: NOT FOUND (run menuconfig first)"
    fi
    
    echo ""
    
    if [ -f "output/images/sdcard.img" ]; then
        SIZE=$(du -h output/images/sdcard.img | cut -f1)
        echo "  Image: EXISTS ($SIZE)"
        echo "  Path:  ./output/images/sdcard.img"
    else
        echo "  Image: NOT BUILT"
    fi
    
    echo ""
    
    if [ -d "dl" ]; then
        DL_SIZE=$(du -sh dl 2>/dev/null | cut -f1)
        echo "  Download cache: $DL_SIZE"
    fi
    
    echo "=========================================="
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
    *)
        show_help
        ;;
esac