# ⚡ Quick Start Guide - STM32 Oscilloscope

## 🎯 Mục tiêu
Chạy oscilloscope trong **5 phút**!

---

## 🔌 Bước 1: Kết nối phần cứng

```
STM32 Blue Pill          Raspberry Pi 4
    PA5 (SCK)    ────>    Pin 23 (GPIO11)
    PA6 (MISO)   ────>    Pin 21 (GPIO9)
    PA7 (MOSI)   ────>    Pin 19 (GPIO10)
    PA4 (NSS)    ────>    Pin 24 (GPIO8)
    GND          ────>    Pin 6  (GND)

    PA0 (ADC)    ────>    [Tín hiệu cần đo: 0-3.3V]
```

**⚠️ CHÚ Ý**: Không nối nguồn 5V/3.3V giữa STM32 và Pi4!

---

## 💻 Bước 2: Cài đặt trên Raspberry Pi 4

### 2.1 Enable SPI
```bash
sudo raspi-config
# Chọn: Interface Options -> SPI -> Enable
sudo reboot
```

### 2.2 Cài Qt5
```bash
sudo apt update
sudo apt install -y qt5-default qtbase5-dev libqt5widgets5 build-essential
```

### 2.3 Add user vào nhóm SPI
```bash
sudo usermod -a -G spi $USER
# Logout và login lại
```

---

## 🔨 Bước 3: Build app

```bash
cd /home/user/2
./build.sh
```

Hoặc:

```bash
make
```

---

## ▶️ Bước 4: Chạy

```bash
./scope
```

Nếu lỗi permission:
```bash
sudo ./scope
```

---

## 🎛️ Sử dụng cơ bản

### Controls

| Control | Chức năng |
|---------|-----------|
| **TRIGGER** checkbox | Bật/tắt trigger (mặc định: ON) |
| **500µs/1ms/2ms/5ms** | Time scale (thời gian/div) |
| **0.5V/1.0V/2.0V** | Voltage scale (điện áp/div) |

### Measurements trên màn hình

- **Samples**: Số lượng điểm dữ liệu
- **Vpp**: Điện áp peak-to-peak
- **Vmax**: Điện áp cực đại
- **Vmin**: Điện áp cực tiểu

### Statistics Panel (bên phải)

- **FPS**: Frame per second
- **Total/Good**: Số gói tin
- **Success %**: Tỷ lệ thành công
- **Timing**: Thống kê thời gian

### Debug Log (phía dưới)

Hiển thị log realtime:
- Packet received
- Trigger events
- Errors & warnings

---

## 🧪 Test nhanh

### Test 1: Đo điện áp DC

```bash
# Nối PA0 vào GND -> sẽ thấy ~0V
# Nối PA0 vào 3.3V -> sẽ thấy ~3.3V
```

### Test 2: Đo sóng vuông từ PWM

```bash
# STM32 có PWM output ở PA8 (TIM1_CH1)
# Nối PA8 -> PA0
# Sẽ thấy sóng vuông ~1kHz
```

### Test 3: Đo tín hiệu audio

```bash
# Dùng audio jack hoặc function generator
# Output: 1-2Vpp @ 1kHz
# Nối vào PA0 qua divider (nếu >3.3V)
```

---

## ❌ Troubleshooting

### Không thấy waveform

1. **Check LED trên STM32**
   - LED PC13 phải nhấp nháy
   - Nếu không: firmware chưa chạy

2. **Check SPI connection**
   ```bash
   ls -l /dev/spidev0.0
   # Phải thấy: crw-rw---- 1 root spi ...
   ```

3. **Check Debug Log**
   - Xem có "First packet received" không
   - Nếu không: kiểm tra kết nối dây

### FPS = 0

- STM32 chưa gửi dữ liệu
- Kiểm tra kết nối MISO (PA6 -> GPIO9)
- Kiểm tra GND chung

### "Marker not found"

- Nhiễu trên dây SPI
- Giảm tốc độ SPI xuống 4MHz (sửa trong code)
- Dùng dây ngắn hơn (<20cm)

### Permission denied

```bash
# Thêm user vào nhóm spi
sudo usermod -a -G spi $USER
# Hoặc chạy với sudo
sudo ./scope
```

---

## 📚 Đọc thêm

- **README_PI4.md**: Hướng dẫn chi tiết
- **PROJECT_OVERVIEW.md**: Tài liệu kỹ thuật đầy đủ
- **ok/Core/Src/README.md**: Tài liệu firmware STM32

---

## 🎉 Kết quả mong đợi

Khi chạy thành công, bạn sẽ thấy:

```
✅ SPI initialized: 8MHz, Mode 0, 8 bits
✅ Reader loop started
📦 First packet received!
📦 Frame #0 | V=1.650V, 1.652V, 1.648V, 1.651V
🎯 Trigger! Starting capture...
✅ Capture complete! 3000 samples
```

Và trên GUI:
- Waveform màu vàng trên grid xanh
- FPS: 40-60
- Success rate: >99%

**Chúc bạn thành công! 🚀**
