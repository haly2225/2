#!/bin/bash

# STM32 Oscilloscope - Pi4 Build Script
# Author: STM32 Oscilloscope Project
# Date: 2024

set -e

echo "🔨 Building STM32 Oscilloscope for Raspberry Pi 4..."
echo ""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if running on Pi
if [ ! -f /proc/device-tree/model ] || ! grep -q "Raspberry Pi" /proc/device-tree/model 2>/dev/null; then
    echo -e "${YELLOW}⚠️  Warning: Not running on Raspberry Pi${NC}"
    echo "   This may still work on other ARM/x86 Linux systems with SPI support"
    echo ""
fi

# Check Qt5
echo -n "Checking Qt5... "
if ! pkg-config --exists Qt5Widgets; then
    echo -e "${RED}❌ FAILED${NC}"
    echo ""
    echo "Qt5 not found. Installing..."
    echo "Run: sudo apt install -y qt5-default qtbase5-dev libqt5widgets5"
    exit 1
else
    QT_VERSION=$(pkg-config --modversion Qt5Widgets)
    echo -e "${GREEN}✓${NC} Found Qt ${QT_VERSION}"
fi

# Check g++
echo -n "Checking g++... "
if ! command -v g++ &> /dev/null; then
    echo -e "${RED}❌ FAILED${NC}"
    echo "g++ not found. Install with: sudo apt install build-essential"
    exit 1
else
    GCC_VERSION=$(g++ --version | head -n1 | awk '{print $NF}')
    echo -e "${GREEN}✓${NC} Found g++ ${GCC_VERSION}"
fi

# Check SPI device
echo -n "Checking SPI device... "
if [ ! -e /dev/spidev0.0 ]; then
    echo -e "${YELLOW}⚠️  WARNING${NC}"
    echo "   /dev/spidev0.0 not found!"
    echo "   Enable SPI: sudo raspi-config -> Interface Options -> SPI"
    echo "   Continuing build anyway..."
else
    echo -e "${GREEN}✓${NC} /dev/spidev0.0 found"
fi

# Check source file
echo -n "Checking source file... "
if [ ! -f pi4.cpp ]; then
    echo -e "${RED}❌ FAILED${NC}"
    echo "pi4.cpp not found in current directory!"
    exit 1
else
    echo -e "${GREEN}✓${NC} pi4.cpp found"
fi

echo ""
echo "Starting compilation..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Compile
g++ -o scope pi4.cpp -std=c++17 \
    $(pkg-config --cflags --libs Qt5Widgets) \
    -fPIC \
    -O2 \
    -Wall

if [ $? -eq 0 ]; then
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo -e "${GREEN}✅ Build SUCCESS!${NC}"
    echo ""
    echo "Executable: ./scope"
    echo "Size: $(ls -lh scope | awk '{print $5}')"
    echo ""
    echo "To run:"
    echo "  ./scope"
    echo ""
    echo "Or with sudo (if SPI permission needed):"
    echo "  sudo ./scope"
    echo ""

    # Check if user is in spi group
    if ! groups | grep -q spi; then
        echo -e "${YELLOW}💡 TIP: Add yourself to 'spi' group to run without sudo:${NC}"
        echo "   sudo usermod -a -G spi $USER"
        echo "   (then logout and login again)"
        echo ""
    fi
else
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo -e "${RED}❌ Build FAILED!${NC}"
    echo ""
    echo "Common issues:"
    echo "1. Missing Qt5: sudo apt install qt5-default qtbase5-dev"
    echo "2. Missing g++: sudo apt install build-essential"
    echo "3. Check error messages above"
    exit 1
fi
