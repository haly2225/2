#!/bin/bash

# Test SPI với nhiều tốc độ khác nhau

echo "🧪 Testing SPI at different speeds..."
echo ""

SPI_SPEEDS=(8000000 4000000 2000000 1000000)

for speed in "${SPI_SPEEDS[@]}"; do
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "Testing at $((speed / 1000000)) MHz..."
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    # Create temp test program
    cat > /tmp/spi_test.cpp << EOF
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <iostream>
#include <vector>

int main() {
    int fd = open("/dev/spidev0.0", O_RDWR);
    if (fd < 0) {
        std::cerr << "Cannot open SPI" << std::endl;
        return 1;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = $speed;

    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    std::vector<uint8_t> rx(516);
    std::vector<uint8_t> tx(516, 0x00);

    int good = 0, total = 0;

    for (int i = 0; i < 100; i++) {
        struct spi_ioc_transfer tr{};
        tr.tx_buf = reinterpret_cast<uint64_t>(tx.data());
        tr.rx_buf = reinterpret_cast<uint64_t>(rx.data());
        tr.len = 516;
        tr.speed_hz = speed;
        tr.bits_per_word = 8;

        if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) >= 0) {
            total++;
            // Find marker
            for (size_t j = 0; j < rx.size() - 1; j++) {
                if (rx[j] == 0xAA && rx[j+1] == 0x55) {
                    good++;
                    break;
                }
            }
        }
        usleep(1000); // 1ms delay
    }

    close(fd);

    std::cout << "Results: " << good << "/" << total
              << " (" << (100.0 * good / total) << "%)" << std::endl;

    return (good > 90) ? 0 : 1;
}
EOF

    # Compile and run
    g++ -o /tmp/spi_test /tmp/spi_test.cpp -std=c++17
    if /tmp/spi_test; then
        echo "✅ GOOD - This speed works well!"
    else
        echo "⚠️  POOR - Try slower speed"
    fi
    echo ""
    sleep 1
done

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "✅ Test complete!"
echo ""
echo "Recommended: Use the highest speed with >95% success rate"
