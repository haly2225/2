#include <iostream>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

using namespace std;

// SPI config
const char* SPI_DEV = "/dev/spidev0.0";
const uint32_t SPI_SPEED = 8000000;  // 8MHz
const uint8_t SPI_MODE = SPI_MODE_0;
const uint8_t SPI_BITS = 8;

const int PACKET_SIZE = 516;  // 4 header + 512 data

void hexDump(const uint8_t* data, int len) {
    for (int i = 0; i < len; i++) {
        if (i % 16 == 0) {
            if (i > 0) cout << endl;
            cout << setfill('0') << setw(4) << hex << i << ": ";
        }
        cout << setfill('0') << setw(2) << hex << (int)data[i] << " ";
    }
    cout << dec << endl;
}

int main() {
    // Open SPI device
    int spi_fd = open(SPI_DEV, O_RDWR);
    if (spi_fd < 0) {
        cerr << "Error: Cannot open " << SPI_DEV << endl;
        return 1;
    }

    // Configure SPI
    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &SPI_MODE) < 0 ||
        ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &SPI_BITS) < 0 ||
        ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &SPI_SPEED) < 0) {
        cerr << "Error: SPI config failed" << endl;
        close(spi_fd);
        return 1;
    }

    cout << "SPI Debug Tool - Reading raw data from STM32" << endl;
    cout << "Press Ctrl+C to stop" << endl << endl;

    uint8_t rx_buf[PACKET_SIZE];
    uint8_t tx_buf[PACKET_SIZE];
    memset(tx_buf, 0, PACKET_SIZE);  // Send dummy bytes

    struct spi_ioc_transfer transfer = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = (unsigned long)rx_buf,
        .len = PACKET_SIZE,
        .speed_hz = SPI_SPEED,
        .bits_per_word = SPI_BITS,
    };

    int frame_count = 0;
    int marker_found = 0;
    int marker_missing = 0;

    while (true) {
        // Transfer data
        if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &transfer) < 0) {
            cerr << "SPI transfer error" << endl;
            break;
        }

        frame_count++;

        // Find marker in buffer (not at fixed position!)
        int marker_pos = -1;
        for (int i = 0; i <= PACKET_SIZE - 4; i++) {
            if (rx_buf[i] == 0xAA && rx_buf[i+1] == 0x55) {
                marker_pos = i;
                break;
            }
        }

        if (marker_pos >= 0) {
            uint16_t frame_id = (rx_buf[marker_pos + 2] << 8) | rx_buf[marker_pos + 3];
            marker_found++;

            cout << "\n=== Frame #" << dec << frame_count
                 << " | STM32 Frame ID: " << frame_id
                 << " | Marker at offset: " << marker_pos << " ===" << endl;

            // Show first 64 bytes
            cout << "First 64 bytes:" << endl;
            hexDump(rx_buf, 64);

            // Parse first 4 ADC samples (after 4-byte header)
            cout << "\nFirst 4 samples (16-bit ADC):" << endl;
            for (int i = 0; i < 4; i++) {
                int offset = marker_pos + 4 + i*2;
                if (offset + 1 < PACKET_SIZE) {
                    uint16_t val = (rx_buf[offset] << 8) | rx_buf[offset + 1];
                    float voltage = val * 3.3f / 4096.0f;
                    cout << "  Sample[" << i << "]: 0x" << hex << val
                         << dec << " = " << voltage << "V" << endl;
                }
            }

        } else {
            marker_missing++;
            cout << "\n!!! Frame #" << dec << frame_count
                 << " - MARKER NOT FOUND !!!" << endl;
            cout << "First 16 bytes (expected 0xAA 0x55 somewhere):" << endl;
            hexDump(rx_buf, 16);
        }

        // Statistics
        float success_rate = 100.0f * marker_found / frame_count;
        cout << "\nStats: " << marker_found << " OK, "
             << marker_missing << " FAIL ("
             << fixed << setprecision(1) << success_rate << "% success)" << endl;

        usleep(50000);  // 50ms delay between reads
    }

    close(spi_fd);
    return 0;
}
