#!/bin/bash

# STM32 Oscilloscope Diagnostic Tool
# Tự động phát hiện vấn đề

echo "🔍 STM32 Oscilloscope Diagnostic Tool"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

PASS=0
FAIL=0
WARN=0

# Function to test
test_item() {
    local name="$1"
    local status="$2"
    local message="$3"

    if [ "$status" == "PASS" ]; then
        echo -e "${GREEN}✅ PASS${NC} | $name"
        [ -n "$message" ] && echo "         $message"
        ((PASS++))
    elif [ "$status" == "FAIL" ]; then
        echo -e "${RED}❌ FAIL${NC} | $name"
        [ -n "$message" ] && echo "         $message"
        ((FAIL++))
    elif [ "$status" == "WARN" ]; then
        echo -e "${YELLOW}⚠️  WARN${NC} | $name"
        [ -n "$message" ] && echo "         $message"
        ((WARN++))
    else
        echo -e "${BLUE}ℹ️  INFO${NC} | $name"
        [ -n "$message" ] && echo "         $message"
    fi
}

# ============================================================================
# Test 1: SPI Device
# ============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📡 Test 1: SPI Device"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ -e /dev/spidev0.0 ]; then
    test_item "SPI Device exists" "PASS" "/dev/spidev0.0 found"

    # Check permissions
    if [ -r /dev/spidev0.0 ] && [ -w /dev/spidev0.0 ]; then
        test_item "SPI Permissions" "PASS" "Read/Write OK"
    else
        test_item "SPI Permissions" "FAIL" "Run: sudo usermod -a -G spi \$USER"
    fi
else
    test_item "SPI Device exists" "FAIL" "Enable SPI: sudo raspi-config"
fi

# Check if user in spi group
if groups | grep -q spi; then
    test_item "User in spi group" "PASS"
else
    test_item "User in spi group" "WARN" "Run: sudo usermod -a -G spi \$USER"
fi

echo ""

# ============================================================================
# Test 2: GPIO Status
# ============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🔌 Test 2: GPIO Pins"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Check if gpio command exists
if command -v gpio &> /dev/null; then
    test_item "GPIO tools installed" "PASS"

    # Try to read GPIO
    if gpio readall &> /dev/null; then
        test_item "GPIO readable" "PASS" "gpio readall works"
    else
        test_item "GPIO readable" "WARN" "May need sudo"
    fi
else
    test_item "GPIO tools" "WARN" "Install: sudo apt install wiringpi"
fi

echo ""

# ============================================================================
# Test 3: Build Tools
# ============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🔧 Test 3: Build Dependencies"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Check g++
if command -v g++ &> /dev/null; then
    GCC_VER=$(g++ --version | head -n1 | awk '{print $NF}')
    test_item "g++ compiler" "PASS" "Version: $GCC_VER"
else
    test_item "g++ compiler" "FAIL" "Install: sudo apt install build-essential"
fi

# Check Qt5
if pkg-config --exists Qt5Widgets; then
    QT_VER=$(pkg-config --modversion Qt5Widgets)
    test_item "Qt5 Widgets" "PASS" "Version: $QT_VER"
else
    test_item "Qt5 Widgets" "FAIL" "Install: sudo apt install qt5-default qtbase5-dev"
fi

# Check if binary exists
if [ -f ./scope ]; then
    SIZE=$(ls -lh ./scope | awk '{print $5}')
    test_item "scope binary" "PASS" "Size: $SIZE"
else
    test_item "scope binary" "WARN" "Run: ./build.sh to compile"
fi

echo ""

# ============================================================================
# Test 4: Quick SPI Communication Test
# ============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📡 Test 4: SPI Communication"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ -e /dev/spidev0.0 ]; then
    # Create minimal test
    cat > /tmp/spi_quick_test.cpp << 'EOF'
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <iostream>
#include <vector>

int main() {
    int fd = open("/dev/spidev0.0", O_RDWR);
    if (fd < 0) return 1;

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = 4000000;

    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    std::vector<uint8_t> rx(516);
    std::vector<uint8_t> tx(516, 0x00);

    int marker_found = 0;
    int total = 0;

    for (int i = 0; i < 20; i++) {
        struct spi_ioc_transfer tr{};
        tr.tx_buf = reinterpret_cast<uint64_t>(tx.data());
        tr.rx_buf = reinterpret_cast<uint64_t>(rx.data());
        tr.len = 516;
        tr.speed_hz = speed;
        tr.bits_per_word = 8;

        if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) >= 0) {
            total++;
            for (size_t j = 0; j < 514; j++) {
                if (rx[j] == 0xAA && rx[j+1] == 0x55) {
                    marker_found++;
                    break;
                }
            }
        }
        usleep(2000);
    }

    close(fd);

    std::cout << marker_found << "/" << total << std::endl;
    return (marker_found >= 15) ? 0 : 1;
}
EOF

    # Compile
    if g++ -o /tmp/spi_quick_test /tmp/spi_quick_test.cpp -std=c++17 2>/dev/null; then
        # Run test
        RESULT=$(/tmp/spi_quick_test 2>&1)

        if [ $? -eq 0 ]; then
            test_item "SPI transfer test" "PASS" "Marker found: $RESULT (Good!)"
        else
            test_item "SPI transfer test" "FAIL" "Marker found: $RESULT (Low success rate)"
            echo "         Possible causes:"
            echo "         1. STM32 not powered or not running firmware"
            echo "         2. MISO wire (PA6→GPIO9) not connected"
            echo "         3. GND not common between STM32 and Pi"
        fi
    else
        test_item "SPI transfer test" "WARN" "Cannot compile test"
    fi

    rm -f /tmp/spi_quick_test /tmp/spi_quick_test.cpp
else
    test_item "SPI transfer test" "SKIP" "SPI device not available"
fi

echo ""

# ============================================================================
# Summary
# ============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📊 Summary"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${GREEN}✅ PASS: $PASS${NC}"
echo -e "${YELLOW}⚠️  WARN: $WARN${NC}"
echo -e "${RED}❌ FAIL: $FAIL${NC}"
echo ""

if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}🎉 All critical tests passed!${NC}"
    echo ""
    echo "You can run the oscilloscope:"
    echo "  ./scope"
    echo ""
else
    echo -e "${RED}⚠️  Some tests failed. Check messages above.${NC}"
    echo ""
    echo "Common fixes:"
    echo "  1. Enable SPI: sudo raspi-config"
    echo "  2. Install Qt5: sudo apt install qt5-default qtbase5-dev"
    echo "  3. Add to spi group: sudo usermod -a -G spi \$USER"
    echo "  4. Check STM32 connections (especially GND and MISO)"
    echo ""
fi

# ============================================================================
# Recommendations
# ============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "💡 Recommendations"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

echo "Hardware checklist:"
echo "  ☐ GND: STM32 GND ←→ Pi4 Pin 6"
echo "  ☐ MISO: STM32 PA6 ←→ Pi4 GPIO9 (Pin 21)"
echo "  ☐ SCK: STM32 PA5 ←→ Pi4 GPIO11 (Pin 23)"
echo "  ☐ NSS: STM32 PA4 ←→ Pi4 GPIO8 (Pin 24)"
echo "  ☐ TEST: STM32 PA8 ←→ PA0 (PWM to ADC for testing)"
echo ""

echo "Next steps:"
echo "  1. Read DEBUG_GUIDE.md for detailed troubleshooting"
echo "  2. Run ./scope and check debug log"
echo "  3. If marker fails > 10%, reduce SPI speed to 4MHz"
echo ""

exit $FAIL
