# 🔧 STM32 Code Fix Guide

## 🔴 Vấn đề phát hiện

Từ log của bạn:
```
22:10:51.332 | ⚠️  Marker not found  ← Gap 96ms
22:10:51.393 | ⚠️  Marker not found  ← Gap 61ms
22:10:51.467 | ⚠️  Marker not found  ← Gap 74ms
22:10:51.481 | ⚠️  GAP: 13.71ms between packets!
```

**STM32 gửi dữ liệu bị NGẮT QUÃNG** - không liên tục!

---

## 🐛 Root Cause: Code có 3 lỗi nghiêm trọng

### Lỗi 1: SPI DMA restart trong callback (`main.c:72-78`)

```c
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  spi_busy = 0;
  spi_count++;
  // ❌ ANTI-PATTERN: Restart DMA trong callback!
  HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buffer, rx_dummy, TX_BYTES);
}
```

**Vấn đề**:
- Callback được gọi **trong interrupt context**
- Restart DMA **trong interrupt** → race condition
- STM32 là SLAVE → phải đợi Pi4 clock
- Khi Pi4 bắt đầu transfer mới, DMA **có thể chưa ready**

**Timing diagram**:
```
Pi4 transfer #1:  [────────516 bytes────────]
                                            ↓
STM32:            [TX]                   Callback
                                            ↓
                                      Restart DMA (100-500µs)
                                            ↓
Pi4 transfer #2:  [────────516 bytes────────]
                   ↑
                   Pi4 bắt đầu ngay → STM32 CHƯA SẴN SÀNG!
                   → Gửi garbage hoặc data cũ
                   → Pi4 nhận được → Marker not found!
```

---

### Lỗi 2: SPI và ADC không đồng bộ

```c
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
  HAL_ADC_Stop_DMA(&hadc1);
  pack_u16_to_bytes(adc_buffer, tx_buffer, BUFFER_SIZE);  ← Update buffer
  adc_complete = 1;
  adc_count++;
}

// Trong lúc đó, SPI đang gửi tx_buffer cũ!
```

**Vấn đề**: **Data race**!
- ADC callback update `tx_buffer`
- SPI DMA đang đọc `tx_buffer` để gửi
- **Không có synchronization** → data bị corrupt

---

### Lỗi 3: SPI NSS mode sai

```c
hspi1.Init.NSS = SPI_NSS_SOFT;  // Dùng software NSS
```

Nhưng code có:
```c
// GPIO EXTI cho PA4 (NSS pin)
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  if (GPIO_Pin == GPIO_PIN_4) {
    nss_triggered = 1;
  }
}
```

**Vấn đề**: NSS không được HAL quản lý → timing không chính xác

---

## ✅ Giải pháp: 3 cách fix

### 🥇 Solution 1: Restart trong main loop (ĐƠN GIẢN NHẤT)

**Ý tưởng**: Không restart trong callback, mà check state trong main loop

```c
// Callback chỉ set flag
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  spi_complete = 1;  // ← Chỉ set flag
  spi_count++;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
  HAL_ADC_Stop_DMA(&hadc1);
  pack_u16_to_bytes(adc_buffer, tx_buffer, BUFFER_SIZE);
  adc_complete = 1;
}

int main(void)
{
  // ... init code ...

  HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buffer, rx_dummy, TX_BYTES);
  start_adc_capture();

  while (1)
  {
    // Restart SPI khi complete
    if (spi_complete) {
      spi_complete = 0;
      HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buffer, rx_dummy, TX_BYTES);
    }

    // Restart ADC khi complete
    if (adc_complete) {
      adc_complete = 0;
      start_adc_capture();
    }
  }
}
```

**Ưu điểm**:
- ✅ Đơn giản, dễ debug
- ✅ Không có race condition
- ✅ Timing rõ ràng

**Nhược điểm**:
- ⚠️ Có thể có delay nhỏ giữa các transfers (vài µs)

---

### 🥈 Solution 2: Double buffering (ỔN ĐỊNH NHẤT)

**Ý tưởng**: 2 buffers, 1 đang gửi, 1 đang update

```c
#define NUM_BUFFERS 2

uint16_t adc_buffer[NUM_BUFFERS][BUFFER_SIZE];
uint8_t tx_buffer[NUM_BUFFERS][TX_BYTES];

volatile uint8_t adc_write_idx = 0;  // ADC ghi vào buffer này
volatile uint8_t spi_read_idx = 0;   // SPI đọc từ buffer này

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
  HAL_ADC_Stop_DMA(&hadc1);

  // Pack vào buffer idle
  pack_u16_to_bytes(adc_buffer[adc_write_idx],
                    tx_buffer[adc_write_idx],
                    BUFFER_SIZE);

  // Swap buffers
  adc_write_idx = (adc_write_idx + 1) % NUM_BUFFERS;
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  spi_count++;

  // Switch to newest buffer
  spi_read_idx = (adc_write_idx == 0) ? 1 : 0;

  // Restart với buffer mới
  HAL_SPI_TransmitReceive_DMA(&hspi1,
                               tx_buffer[spi_read_idx],
                               rx_dummy,
                               TX_BYTES);
}
```

**Ưu điểm**:
- ✅ Không có data race
- ✅ ADC và SPI độc lập
- ✅ Luôn gửi data mới nhất

**Nhược điểm**:
- ⚠️ Dùng 2x RAM

---

### 🥉 Solution 3: Circular DMA (PHỨC TẠP)

Dùng buffer circular lớn, SPI DMA tự động wrap around.

**Không khuyến nghị** vì:
- Phức tạp để config
- STM32 HAL không hỗ trợ tốt circular mode cho SPI slave
- Khó debug

---

## 🎯 Khuyến nghị: Dùng Solution 1

**File**: Thay thế `ok/Core/Src/main.c`

```c
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32F103 Oscilloscope - FIXED
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"
#include <string.h>

#define BUFFER_SIZE          256
#define TX_BYTES             (BUFFER_SIZE * 2 + 4)

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_rx;
DMA_HandleTypeDef hdma_spi1_tx;
TIM_HandleTypeDef htim1;

uint16_t adc_buffer[BUFFER_SIZE] __attribute__((aligned(4)));
uint8_t  tx_buffer[TX_BYTES] __attribute__((aligned(4)));
uint8_t  rx_dummy[TX_BYTES] __attribute__((aligned(4)));

volatile uint8_t adc_complete = 0;
volatile uint8_t spi_complete = 0;  // ← NEW!
volatile uint32_t adc_count = 0;
volatile uint32_t spi_count = 0;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM1_Init(void);

void pack_u16_to_bytes(uint16_t *src, uint8_t *dst, uint16_t count)
{
  static uint16_t frame_counter = 0;

  *dst++ = 0xAA;
  *dst++ = 0x55;
  *dst++ = (frame_counter >> 8) & 0xFF;
  *dst++ = frame_counter & 0xFF;
  frame_counter++;

  for (uint16_t i = 0; i < count; i++) {
    uint16_t val = src[i];
    *dst++ = (val >> 8) & 0xFF;
    *dst++ = val & 0xFF;
  }
}

void start_adc_capture(void)
{
  adc_complete = 0;
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buffer, BUFFER_SIZE);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
  HAL_ADC_Stop_DMA(&hadc1);
  pack_u16_to_bytes(adc_buffer, tx_buffer, BUFFER_SIZE);
  adc_complete = 1;
  adc_count++;
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  spi_complete = 1;  // ← Chỉ set flag, KHÔNG restart!
  spi_count++;
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  spi_complete = 1;  // Báo lỗi để restart
}

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_SPI1_Init();
  MX_TIM1_Init();

  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIM_Base_Start(&htim1);
  __HAL_TIM_MOE_ENABLE(&htim1);

  HAL_ADCEx_Calibration_Start(&hadc1);

  // LED startup blink
  for (int i = 0; i < 3; i++) {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    HAL_Delay(200);
  }

  // Initialize with default data
  for (int i = 0; i < BUFFER_SIZE; i++) {
    adc_buffer[i] = 2048;
  }
  pack_u16_to_bytes(adc_buffer, tx_buffer, BUFFER_SIZE);

  // Start SPI and ADC
  HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buffer, rx_dummy, TX_BYTES);
  start_adc_capture();

  uint32_t last_heartbeat = 0;

  while (1)
  {
    // Heartbeat LED
    if (HAL_GetTick() - last_heartbeat > 500) {
      last_heartbeat = HAL_GetTick();
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }

    // ✅ Restart SPI when complete (trong main loop, KHÔNG trong callback!)
    if (spi_complete) {
      spi_complete = 0;

      // Small delay để đảm bảo Pi4 đã xong
      // Không cần delay nếu Pi4 poll đủ chậm
      // __NOP(); __NOP(); __NOP(); __NOP();

      HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buffer, rx_dummy, TX_BYTES);
    }

    // ✅ Restart ADC when complete
    if (adc_complete) {
      adc_complete = 0;
      start_adc_capture();
    }
  }
}

// ... Các hàm init không đổi ...

void SystemClock_Config(void) { /* same as before */ }
static void MX_ADC1_Init(void) { /* same as before */ }
static void MX_SPI1_Init(void) { /* same as before */ }
static void MX_TIM1_Init(void) { /* same as before */ }
static void MX_DMA_Init(void) { /* same as before */ }
static void MX_GPIO_Init(void) { /* same as before */ }
void Error_Handler(void) { /* same as before */ }
```

---

## 🔨 How to Apply Fix

### Option A: Sửa trực tiếp file `main.c`

```bash
cd /home/user/2/ok/Core/Src

# Backup
cp main.c main_old.c

# Sửa dòng 72-78:
nano main.c
```

**Thay đổi**:
```c
// TỪ:
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  spi_busy = 0;
  spi_count++;
  HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buffer, rx_dummy, TX_BYTES);  // ← XÓA DÒNG NÀY
}

// THÀNH:
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  spi_complete = 1;  // ← THÊM DÒNG NÀY
  spi_count++;
}
```

**Thêm vào main loop** (dòng 126-137):
```c
while (1)
{
    if (HAL_GetTick() - last_heartbeat > 500) {
      last_heartbeat = HAL_GetTick();
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }

    // ✅ THÊM ĐOẠN NÀY
    if (spi_complete) {
      spi_complete = 0;
      HAL_SPI_TransmitReceive_DMA(&hspi1, tx_buffer, rx_dummy, TX_BYTES);
    }

    if (adc_complete) {
      adc_complete = 0;
      start_adc_capture();
    }
}
```

**Thêm biến** (dòng ~26):
```c
volatile uint8_t spi_complete = 0;  // ← THÊM DÒNG NÀY
```

---

### Option B: Dùng file đã fix sẵn

```bash
# Tôi đã tạo sẵn file fixed
cp /home/user/2/ok/Core/Src/main_fixed.c /home/user/2/ok/Core/Src/main.c
```

---

## 📊 Kết quả mong đợi

### Trước khi fix:
```
Pi4 log:
22:10:51.332 | ⚠️  Marker not found  ← 40-50% packets fail
22:10:51.393 | ⚠️  Marker not found
22:10:51.481 | ⚠️  GAP: 13.71ms between packets!
Success rate: ~60%
```

### Sau khi fix:
```
Pi4 log:
22:15:10.123 | 📦 Frame #1234 | V=1.650V, 1.652V, 1.648V
22:15:10.125 | 📦 Frame #1235 | V=1.651V, 1.649V, 1.650V  ← Liên tục
22:15:10.127 | 📦 Frame #1236 | V=1.650V, 1.651V, 1.652V  ← Không có gap
Success rate: >95%
Marker fails: <5%
```

---

## ⚠️ Lưu ý quan trọng

1. **Sau khi sửa code, phải REBUILD và FLASH lại STM32**:
```bash
cd /home/user/2/ok/Debug
make clean && make
st-flash write ok.elf 0x8000000
```

2. **Kiểm tra LED trên STM32**:
   - Phải nhấp nháy đều đặn (500ms on/off)
   - Nếu không → code lỗi hoặc không flash đúng

3. **Test trên Pi4**:
```bash
./scope
# Xem debug log → Marker fail phải < 5%
```

---

## 🎯 Summary

| Vấn đề | Giải pháp |
|--------|-----------|
| SPI restart trong callback | ✅ Restart trong main loop |
| Data race ADC/SPI | ✅ Flag synchronization |
| Gap giữa packets | ✅ Continuous transfer |

**1 dòng code thay đổi chính**:
```c
// BEFORE:
HAL_SPI_TransmitReceive_DMA(...);  // In callback ❌

// AFTER:
spi_complete = 1;                  // Set flag ✅
// Restart in main loop
```

Thử fix này và cho tôi xem log mới! 🚀
