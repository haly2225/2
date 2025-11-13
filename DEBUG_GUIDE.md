# 🐛 Debug Guide - Phân tích log của bạn

## 📊 Phân tích log output

### Log của bạn:
```
22:10:51.230 | 📦 First packet received!
22:10:51.230 | ⚠️  Marker not found
22:10:51.235 | 📦 Frame #5949 | V=3.297V, 3.295V, 3.293V, 3.290V
22:10:51.236 | 📦 Frame #5950 | V=0.000V, 0.000V, 0.000V, 0.000V
22:10:51.236 | 🎯 Trigger! Starting capture...
22:10:51.332 | ⚠️  Marker not found  ← Xuất hiện nhiều lần
22:10:51.393 | ⚠️  Marker not found
22:10:51.481 | ⚠️  GAP: 13.71ms between packets!
```

---

## 🔴 Vấn đề 1: "Marker not found" (40-50% packets)

### Nguyên nhân:
```
┌─────────────────────────────────────────────────────┐
│ SPI Buffer misalignment                             │
├─────────────────────────────────────────────────────┤
│                                                     │
│ STM32 gửi:  [AA 55 xx xx DATA DATA DATA...]        │
│                                                     │
│ Pi4 đọc:    [xx AA 55 xx DATA DATA DATA...]        │
│             ↑ Shift 1 byte → Marker ở sai vị trí   │
│                                                     │
└─────────────────────────────────────────────────────┘
```

**Tại sao xảy ra**:
1. **NSS timing không chính xác**:
   - STM32 bắt đầu gửi khi NSS LOW
   - Pi4 đọc khi NSS LOW
   - Nếu timing lệch → đọc giữa chừng packet

2. **Buffer overflow**:
   - STM32 gửi liên tục
   - Pi4 đọc chậm hơn
   - Data bị overwrite → mất sync

3. **Clock edge mismatch**:
   - SPI Mode 0: CPOL=0, CPHA=0
   - Data valid on RISING edge
   - Nếu noise trên clock → sample sai

### Giải pháp:

#### A. Kiểm tra kết nối dây
```bash
# Dùng multimeter đo
# 1. Continuity test
GND STM32 ←→ GND Pi4         (phải thông)
PA6 ←→ GPIO9 Pin 21          (phải thông)

# 2. Điện áp khi idle
PA6 (MISO): 0V hoặc 3.3V
PA5 (SCK):  0V (idle low)
PA4 (NSS):  3.3V (idle high)
```

#### B. Thử giảm tốc độ SPI
Sửa file `pi4.cpp` dòng ~201:
```cpp
// uint32_t speed = 8000000;  // 8 MHz - có thể bị lỗi
uint32_t speed = 4000000;     // 4 MHz - ổn định hơn
```

Rebuild:
```bash
make clean && make
./scope
```

#### C. Thêm delay giữa transfers
Sửa file `pi4.cpp` dòng ~321:
```cpp
if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
    logger.log("⚠️  SPI transfer failed");
    continue;
}

// THÊM delay nhỏ
usleep(100);  // 100 microseconds delay
```

---

## 🔴 Vấn đề 2: ADC Input Floating

### Phân tích voltage readings:

```
Frame #5949: 3.297V, 3.295V, 3.293V, 3.290V  ← Gần VCC (3.3V)
Frame #5950: 0.000V, 0.000V, 0.000V, 0.000V  ← All zeros
Frame #5951: 3.295V, 3.294V, 3.296V, 3.296V  ← Lại VCC
```

**Kết luận**: PA0 (ADC input) đang **FLOATING** (không nối gì)

### Tại sao trigger liên tục?

```
┌─────────────────────────────────────────────────────┐
│ Floating pin nhặt nhiễu                             │
├─────────────────────────────────────────────────────┤
│                                                     │
│  Voltage (V)                                        │
│   3.3 ────┐         ┌────  ← Nhiễu từ môi trường   │
│           │         │                               │
│   1.65 ───┼─────────┼────  ← Trigger level (50%)   │
│           │         │                               │
│   0.0  ───┴─────────┴────                           │
│          ↑         ↑                                │
│       Trigger   Trigger (liên tục!)                 │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### Giải pháp:

#### Test 1: Nối PA0 vào GND
```
PA0 ──── GND

Kết quả mong đợi:
- Voltage: ~0.000V - 0.050V (ổn định)
- Trigger: KHÔNG (vì không có edge)
- Marker fail: Sẽ giảm xuống
```

#### Test 2: Nối PA0 vào 3.3V
```
PA0 ──── 3.3V

Kết quả mong đợi:
- Voltage: ~3.250V - 3.300V (ổn định)
- Trigger: KHÔNG
- Marker fail: Sẽ giảm xuống
```

#### Test 3: Nối PA0 vào PWM (TIM1_CH1)
```
PA8 (TIM1 PWM output) ──── PA0 (ADC input)

Kết quả mong đợi:
- Voltage: Sóng vuông 0V ↔ 3.3V
- Frequency: ~1000 Hz
- Trigger: CÓ (rising edge)
- Waveform: Sóng vuông đẹp
```

**QUAN TRỌNG**: Test 3 là cách TỐT NHẤT để kiểm tra!

---

## 🔴 Vấn đề 3: Timing Gaps

```
22:10:51.481 | ⚠️  GAP: 13.71ms between packets!
```

### Nguyên nhân:
- Khi marker fail, code skip packet
- 10-20 packets liên tiếp fail → tạo gap lớn
- Gap >10ms → được report

### Ảnh hưởng:
- FPS giảm
- Waveform bị gián đoạn
- Trigger không ổn định

### Giải pháp:
→ Fix vấn đề 1 (marker detection) sẽ fix luôn vấn đề này

---

## 🧪 Test Plan - Làm theo thứ tự

### Step 1: Kiểm tra phần cứng (5 phút)

```bash
# 1. Test GND
# Dùng multimeter, đo giữa:
STM32 GND pin ←→ Pi4 Pin 6 (GND)
Resistance: < 1Ω

# 2. Test MISO (quan trọng nhất!)
# Khi STM32 đang chạy:
PA6 (STM32) ←→ GPIO9 Pin 21 (Pi4)
Voltage: Dao động 0-3.3V (khi đo bằng oscilloscope)

# 3. Test clock
PA5 (STM32 SCK) ←→ GPIO11 Pin 23 (Pi4)
Khi chạy scope: có xung clock ~8MHz
```

### Step 2: Test ADC input (2 phút)

```bash
# Nối dây test
PA8 ──── PA0

# STM32 tạo PWM ~1kHz ở PA8
# Chạy scope app:
./scope

# Xem log phải thấy:
# - Voltage dao động 0V ↔ 3.3V
# - Trigger bắt được
# - Waveform sóng vuông
```

### Step 3: Giảm SPI speed (5 phút)

```bash
# Edit pi4.cpp
nano pi4.cpp

# Tìm dòng ~201, sửa thành:
uint32_t speed = 4000000;  // Giảm từ 8MHz xuống 4MHz

# Rebuild
make clean && make

# Test
./scope
```

Xem log:
- Marker fail phải giảm từ 40% xuống < 5%
- FPS tăng lên
- Gap ít hơn

### Step 4: Kiểm tra statistics (1 phút)

Chạy 30 giây, xem statistics panel:

```
Kết quả tốt:
✅ Success: > 95%
✅ FPS: > 40
✅ Gaps: < 5
✅ Marker fails: < 100

Kết quả xấu:
❌ Success: < 80%
❌ FPS: < 20
❌ Gaps: > 50
❌ Marker fails: > 1000
```

---

## 📈 Expected Results

### Trước fix (hiện tại của bạn):
```
Success rate: ~60%
Marker fails: 40-50% packets
Gaps: Nhiều (>10)
Waveform: Bị gián đoạn
Trigger: Liên tục (do floating input)
```

### Sau khi fix:
```
Success rate: >95%
Marker fails: <5% packets
Gaps: Hiếm (<3)
Waveform: Liên tục, smooth
Trigger: Ổn định, chỉ bắt khi có edge
```

---

## 🔧 Quick Fixes Summary

### Fix 1: Kết nối PA8 → PA0 (Bắt buộc!)
```bash
# Nối dây jumper ngắn
PA8 (PWM output) ──── PA0 (ADC input)

# Không cần code gì, chỉ cần dây!
```

### Fix 2: Giảm SPI speed
```cpp
// File: pi4.cpp, line ~201
uint32_t speed = 4000000;  // Từ 8MHz → 4MHz
```

### Fix 3: Kiểm tra GND
```bash
# PHẢI có GND chung!
STM32 GND ──── Pi4 Pin 6
```

---

## 🚨 Common Mistakes

### ❌ Lỗi 1: Không có GND chung
```
STM32        Pi4
  VCC ──X──  (không nối)
  GND ──X──  (không nối)  ← SAI!

Hậu quả: Voltage level không chuẩn → marker fail
```

### ❌ Lỗi 2: Nối nhầm MOSI/MISO
```
STM32 PA6 (MISO) ──X── Pi4 GPIO10 (MOSI)  ← SAI!
STM32 PA7 (MOSI) ──X── Pi4 GPIO9 (MISO)   ← SAI!

Đúng phải là:
STM32 PA6 (MISO) ──✓── Pi4 GPIO9 (MISO)
STM32 PA7 (MOSI) ──✓── Pi4 GPIO10 (MOSI)
```

### ❌ Lỗi 3: Dây quá dài
```
Dây SPI > 30cm → Noise, capacitance
→ Marker fail tăng

Giải pháp: Dùng dây < 15cm
```

---

## 📞 Nếu vẫn lỗi sau khi fix

### Test cơ bản nhất: SPI Loopback

```bash
# Tắt STM32
# Nối MOSI và MISO lại với nhau ở phía Pi4
GPIO10 (MOSI) ──── GPIO9 (MISO)

# Test
sudo apt install spi-tools
spi-test -D /dev/spidev0.0 -v -s 4000000

# Phải thấy: TX data == RX data
```

Nếu loopback OK → Vấn đề ở STM32
Nếu loopback FAIL → Vấn đề ở Pi4 (SPI driver)

---

**TL;DR - Làm ngay 3 việc này:**

1. ✅ Nối **PA8 → PA0** (test signal)
2. ✅ Giảm SPI speed từ **8MHz → 4MHz**
3. ✅ Kiểm tra **GND chung**

Sau đó chạy lại `./scope` và cho tôi xem log mới! 🚀
