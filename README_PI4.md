# 🔬 STM32 Oscilloscope - Raspberry Pi 4 Application

Ứng dụng Qt5 để nhận và hiển thị dữ liệu oscilloscope từ STM32F103C8T6 qua SPI.

---

## 📋 Yêu cầu hệ thống

### Phần cứng
- **Raspberry Pi 4** (hoặc Pi 3)
- **STM32F103C8T6** đã flash firmware oscilloscope
- Kết nối SPI theo sơ đồ dưới

### Kết nối phần cứng

| STM32F103C8T6 | Raspberry Pi 4  | Chức năng |
|---------------|-----------------|-----------|
| PA5 (SCK)     | GPIO11 (Pin 23) | SPI Clock |
| PA6 (MISO)    | GPIO9 (Pin 21)  | Master In Slave Out |
| PA7 (MOSI)    | GPIO10 (Pin 19) | Master Out Slave In |
| PA4 (NSS)     | GPIO8 (Pin 24)  | Chip Select |
| GND           | GND (Pin 6)     | Ground |

### Phần mềm
- **Raspberry Pi OS** (Debian-based)
- **Qt5** development libraries
- **g++** với hỗ trợ C++17

---

## 🚀 Cài đặt

### 1. Cài đặt Qt5 và dependencies

```bash
sudo apt update
sudo apt install -y build-essential qt5-default qtbase5-dev libqt5widgets5
sudo apt install -y pkg-config g++
```

### 2. Kích hoạt SPI trên Raspberry Pi

```bash
# Bật SPI interface
sudo raspi-config
# Chọn: Interfacing Options -> SPI -> Enable

# Hoặc chỉnh sửa file config
sudo nano /boot/config.txt
# Thêm dòng: dtparam=spi=on

# Reboot
sudo reboot
```

### 3. Kiểm tra SPI hoạt động

```bash
# Kiểm tra device có tồn tại không
ls -l /dev/spidev0.0
# Output mong đợi: crw-rw---- 1 root spi 153, 0 ...

# Thêm user vào nhóm spi (để chạy không cần sudo)
sudo usermod -a -G spi $USER
# Logout và login lại để áp dụng
```

---

## 🔨 Biên dịch

### Cách 1: Biên dịch thủ công

```bash
g++ -o scope pi4.cpp -std=c++17 \
    $(pkg-config --cflags --libs Qt5Widgets) \
    -fPIC
```

### Cách 2: Sử dụng script build

```bash
chmod +x build.sh
./build.sh
```

Nếu biên dịch thành công, bạn sẽ có file thực thi `scope`.

---

## ▶️ Chạy ứng dụng

### Chạy với quyền thường (khuyến nghị)

```bash
./scope
```

### Chạy với sudo (nếu chưa thêm user vào nhóm spi)

```bash
sudo ./scope
```

---

## 🎛️ Sử dụng

### Giao diện chính

```
┌─────────────────────────────────┬──────────┐
│                                 │ TRIGGER  │
│       Hiển thị sóng             │ TIME/DIV │
│       (Oscilloscope Display)    │ 500µs    │
│                                 │ 1ms      │
│                                 │ 2ms      │
│                                 │ 5ms      │
│                                 │          │
│                                 │ VOLT/DIV │
│                                 │ 0.5V     │
│                                 │ 1.0V     │
│                                 │ 2.0V     │
│                                 │          │
│                                 │ 📊 Stats │
├─────────────────────────────────┴──────────┤
│         📊 DEBUG LOG                       │
│  Thông tin debug và trạng thái realtime    │
└────────────────────────────────────────────┘
```

### Các chức năng

#### 🎯 Trigger Mode
- **Checkbox TRIGGER**: Bật/tắt chế độ trigger
  - ✅ **Bật**: Tự động bắt sóng khi có cạnh lên vượt ngưỡng 50% VCC (1.65V)
  - ❌ **Tắt**: Chế độ free-run, hiển thị liên tục

#### ⏱️ Time Scale
- **500µs/div**: 5ms toàn màn hình
- **1ms/div**: 10ms toàn màn hình
- **2ms/div**: 20ms toàn màn hình
- **5ms/div**: 50ms toàn màn hình

#### 📊 Voltage Scale
- **0.5V/div**: 4V toàn màn hình
- **1.0V/div**: 8V toàn màn hình
- **2.0V/div**: 16V toàn màn hình

#### 📈 Thông số đo
- **Samples**: Số lượng mẫu hiển thị
- **Vpp**: Điện áp peak-to-peak
- **Vmax**: Điện áp cao nhất
- **Vmin**: Điện áp thấp nhất

#### 📊 Statistics Panel
- **FPS**: Frame per second (tốc độ cập nhật)
- **Total/Good packets**: Số gói tin nhận được
- **Success rate**: Tỷ lệ thành công
- **Timing**: Min/Max/Avg interval giữa các gói
- **Errors**: Lỗi marker, ADC, gaps

---

## 🔧 Cấu hình Protocol

### Frame format từ STM32

```
┌──────┬──────┬───────────┬───────────────────────┐
│ 0xAA │ 0x55 │ Counter   │ ADC Data (256 samples)│
│      │      │ (2 bytes) │ (512 bytes)           │
└──────┴──────┴───────────┴───────────────────────┘
Total: 516 bytes per frame
```

### Thông số SPI
- **Speed**: 8 MHz
- **Mode**: 0 (CPOL=0, CPHA=0)
- **Bits**: 8-bit per word
- **STM32 role**: Slave (transmit only)
- **Pi4 role**: Master (receive only)

### ADC Parameters
- **Resolution**: 12-bit (0-4095)
- **Voltage range**: 0-3.3V
- **Samples per frame**: 256
- **Sample rate**: ~250 kHz (có thể thay đổi bằng TIM1 prescaler)

---

## 🐛 Troubleshooting

### Lỗi: "Cannot open /dev/spidev0.0"

**Nguyên nhân**: SPI chưa được kích hoạt

**Giải pháp**:
```bash
sudo raspi-config
# Enable SPI interface
sudo reboot
```

---

### Lỗi: "Permission denied" khi chạy

**Nguyên nhân**: User chưa có quyền truy cập SPI

**Giải pháp**:
```bash
sudo usermod -a -G spi $USER
# Logout và login lại
```

Hoặc chạy với sudo:
```bash
sudo ./scope
```

---

### Không nhận được dữ liệu (FPS = 0)

**Kiểm tra**:
1. Đảm bảo STM32 đã được flash firmware và đang chạy
2. Kiểm tra kết nối dây SPI (đặc biệt GND)
3. Đo điện áp 3.3V từ STM32
4. Kiểm tra LED trên STM32 có nhấp nháy không
5. Xem Debug Log có thông báo lỗi gì

**Debug**:
```bash
# Test SPI loopback
# Nối MOSI và MISO lại với nhau
sudo apt install spi-tools
spi-test -D /dev/spidev0.0 -v
```

---

### Marker not found (nhiều lỗi marker)

**Nguyên nhân**:
- Dây SPI bị nhiễu
- Tốc độ SPI quá cao
- Timing không đồng bộ

**Giải pháp**:
1. Giảm tốc độ SPI xuống 4MHz hoặc 2MHz (sửa trong code: `speed = 4000000`)
2. Sử dụng dây ngắn hơn, có shield
3. Thêm pull-up resistor 10kΩ cho NSS pin

---

### Qt5 not found khi compile

**Giải pháp**:
```bash
# Cài đặt đầy đủ Qt5
sudo apt install qt5-default qtbase5-dev libqt5widgets5

# Hoặc chỉ định path thủ công
export PKG_CONFIG_PATH=/usr/lib/arm-linux-gnueabihf/pkgconfig
```

---

## 📚 Cấu trúc code

### Class chính

1. **SPIReader**:
   - Quản lý kết nối SPI
   - Đọc dữ liệu trong thread riêng
   - Parse protocol và validate data
   - Xử lý trigger logic

2. **ScopeDisplay**:
   - Widget Qt để vẽ waveform
   - Grid oscilloscope style
   - Thang đo time/voltage
   - Hiển thị measurements

3. **MainWindow**:
   - Giao diện chính
   - Control panel
   - Debug log
   - Stats display

4. **DebugLogger**:
   - Ghi log với timestamp
   - Thread-safe
   - Buffer circular

5. **TimingStats**:
   - Đếm packets
   - Tính timing statistics
   - Detect gaps và errors

---

## 🎨 Tùy chỉnh

### Thay đổi tốc độ SPI

Sửa trong `SPIReader::init()`:
```cpp
uint32_t speed = 8000000;  // Đổi thành 4000000 (4MHz) hoặc 2000000 (2MHz)
```

### Thay đổi trigger level

Sửa trong `SPIReader::process_trigger()`:
```cpp
float trigger_level = VCC * 0.50f;  // 50% = 1.65V
float hysteresis = VCC * 0.15f;     // Hysteresis band
```

### Thay đổi capture size

Sửa trong header:
```cpp
constexpr size_t CAPTURE_SIZE = 3000;  // Số samples sau khi trigger
constexpr size_t PRETRIGGER_SIZE = 128; // Số samples trước trigger
```

### Thay đổi màu sắc waveform

Sửa trong `ScopeDisplay::paintEvent()`:
```cpp
p.setPen(QPen(QColor(255, 220, 0), 2));  // Yellow waveform
// Đổi thành QColor(0, 255, 0) cho màu xanh lá
```

---

## 📊 Hiệu suất

### Thông số đo được (Raspberry Pi 4)

- **FPS**: 20-60 fps (tùy trigger mode)
- **Latency**: < 50ms
- **CPU usage**: 15-25%
- **Memory**: ~50 MB

### Tối ưu hóa

Để tăng hiệu suất:
1. Tắt trigger mode (free-run)
2. Giảm update rate của GUI (sửa timer từ 50ms → 100ms)
3. Overclock Raspberry Pi
4. Sử dụng Pi 4 với 4GB RAM trở lên

---

## 📝 License

Code này là mã nguồn mở cho mục đích học tập và phát triển.

---

## 🙏 Credits

- **STM32 HAL Library**: STMicroelectronics
- **Qt5 Framework**: The Qt Company
- **Linux SPI Driver**: Linux Kernel Community

---

## 📧 Hỗ trợ

Nếu gặp vấn đề, hãy kiểm tra:
1. Debug Log trong app
2. Dùng `dmesg | grep spi` để xem kernel messages
3. Test với `spi-test` tool

**Chúc may mắn với dự án oscilloscope! 🎉**
