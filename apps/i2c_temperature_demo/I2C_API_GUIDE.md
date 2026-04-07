# Hướng Dẫn I2C API và SHT3x Sensor cho ESP32

## Mục Lục
1. [Giới Thiệu I2C](#giới-thiệu-i2c)
2. [SHT3x Sensor - Chi Tiết](#sht3x-sensor---chi-tiết)
3. [I2C API Cốt Lõi](#i2c-api-cốt-lõi)
4. [Khởi Tạo và Thiết Lập](#khởi-tạo-và-thiết-lập)
5. [Các Chế Độ Đo Lường](#các-chế-độ-đo-lường)
6. [Xử Lý Lỗi](#xử-lý-lỗi)
7. [Áp Dụng cho Các Cảm Biến I2C Khác](#áp-dụng-cho-các-cảm-biến-i2c-khác)
8. [Tham Khảo](#tham-khảo)

---

## Giới Thiệu I2C

### I2C là gì?
**I2C (Inter-Integrated Circuit)** là giao thức giao tiếp nối tiếp, cho phép một master (ESP32) kiểm soát nhiều slave devices (cảm biến, EEPROM, LCD, v.v.) qua 2 dây:
- **SDA (Serial Data)**: Dây dữ liệu
- **SCL (Serial Clock)**: Dây xung nhịp

### Đặc Điểm I2C
| Tính Chất | Giá Trị |
|-----------|--------|
| Tốc độ chuẩn | 100 kHz (Standard mode) |
| Tốc độ cao | 400 kHz (Fast mode) |
| Tốc độ siêu cao | 1 MHz (Fast mode plus) |
| Số lượng thiết bị | Tối đa 128 thiết bị |
| Số dây | 2 dây (SDA + SCL) |
| Địa chỉ | 7-bit hoặc 10-bit |

### So Sánh với các Giao Thức Khác
| Giao Thức | Dây | Tốc Độ | Khoảng Cách | Điểm Yếu |
|-----------|-----|--------|-----------|----------|
| **I2C** | 2 | 100-400 kHz | Ngắn (<1m) | Chậm |
| **SPI** | 4+ | 1-50 MHz | Ngắn | Nhiều dây |
| **UART** | 2 | 9600-115200 | Trung bình | 1-1 giao tiếp |
| **CAN** | 2 | 1 MHz | Dài (>100m) | Phức tạp |

---

## SHT3x Sensor - Chi Tiết

### Tổng Quan
SHT3x là cảm biến đo nhiệt độ và độ ẩm tương đối với độ chính xác cao, giao tiếp I2C.

### Thông Số Kỹ Thuật

#### Chính Xác
| Tham Số | Giá Trị |
|---------|--------|
| Sai số nhiệt độ | ±0.2°C (20-60°C) |
| Sai số độ ẩm | ±1.5% RH (20-80%) |
| Phạm vi nhiệt độ | -40°C đến +125°C |
| Phạm vi độ ẩm | 0% đến 100% RH |

#### Cấu Hình I2C
| Thông Số | Giá Trị |
|---------|--------|
| Địa chỉ mặc định | 0x44 (ADDR pin nối GND) |
| Địa chỉ thay thế | 0x45 (ADDR pin nối VCC) |
| Tốc độ I2C | 100 kHz - 1 MHz |
| Điện áp cấp | 2.4V - 5.5V (thường 3.3V) |

#### Dòng Điện
| Chế Độ | Dòng Điện |
|--------|----------|
| Đo lường | ~5-15 mA |
| Idle | ~0.5 µA |
| Sleep | ~0.1 µA |

### Chân Kết Nối
```
SHT3x      |  ESP32
-----------|----------
VCC (1)    |  3.3V (NOT 5V!)
GND (2)    |  GND
SCL (3)    |  GPIO22 (I2C Clock)
SDA (4)    |  GPIO21 (I2C Data)
ADDR (5)   |  GND (0x44) or VCC (0x45)
ALERT (6)  |  Optional (GPIO interrupt)
```

**⚠️ Lưu Ý Quan Trọng**: ESP32 là thiết bị 3.3V, KHÔNG dùng 5V để cấp nguồn cho SHT3x!

---

## I2C API Cốt Lõi

### 1. Khởi Tạo I2C Bus
Trước khi sử dụng bất kỳ thiết bị I2C nào, phải khởi tạo bus I2C:

```c
#include <i2cdev.h>

// Khởi tạo I2C bus global
esp_err_t err = i2cdev_init();
if (err != ESP_OK) {
    printf("I2C init failed: %s\n", esp_err_to_name(err));
    return;
}
```

**Giải Thích**: 
- `i2cdev_init()` khởi tạo I2C driver mặc định (bus 0)
- Trả về `ESP_OK` nếu thành công
- Phải gọi trước khi sử dụng bất kỳ thiết bị I2C

### 2. Cấu Trúc Thiết Bị (Device Descriptor)
Mỗi thiết bị I2C cần một cấu trúc để lưu thông tin:

```c
sht3x_t dev;  // Cấu trúc SHT3x

// Khởi tạo bộ mô tả thiết bị
ESP_ERROR_CHECK(sht3x_init_desc(
    &dev,                              // Pointer đến cấu trúc device
    CONFIG_EXAMPLE_SHT3X_ADDR,        // Địa chỉ I2C (0x44)
    0,                                 // I2C master index (0 = I2C0)
    CONFIG_EXAMPLE_I2C_MASTER_SDA,    // GPIO SDA (21)
    CONFIG_EXAMPLE_I2C_MASTER_SCL     // GPIO SCL (22)
));
```

**Giải Thích**:
- `init_desc()`: Tạo bộ mô tả thiết bị
- `dev`: Cấu trúc chứa thông tin thiết bị (địa chỉ, GPIO, v.v.)
- `addr`: Địa chỉ I2C (0x44 hoặc 0x45)
- `port`: I2C bus index (0 hoặc 1)
- `sda`, `scl`: Chân GPIO

### 3. Khởi Tạo Thiết Bị
```c
// Khởi tạo sensor SHT3x
ESP_ERROR_CHECK(sht3x_init(&dev));
```

**Giải Thích**:
- Gửi các lệnh khởi tạo đến sensor
- Kiểm tra xem sensor có phản hồi hay không
- Cấu hình các thông số mặc định

---

## Khởi Tạo và Thiết Lập

### Quy Trình Khởi Tạo Đầy Đủ
```c
#include <stdio.h>
#include <i2cdev.h>
#include <sht3x.h>
#include <esp_err.h>
#include <string.h>

void app_main()
{
    // Bước 1: Khởi tạo I2C bus
    ESP_ERROR_CHECK(i2cdev_init());
    
    // Bước 2: Tạo cấu trúc device và xóa clean
    sht3x_t dev;
    memset(&dev, 0, sizeof(sht3x_t));
    
    // Bước 3: Khởi tạo bộ mô tả device
    ESP_ERROR_CHECK(sht3x_init_desc(
        &dev, 
        0x44,     // Địa chỉ I2C
        0,        // I2C port
        21,       // SDA GPIO
        22        // SCL GPIO
    ));
    
    // Bước 4: Khởi tạo sensor
    ESP_ERROR_CHECK(sht3x_init(&dev));
    
    printf("SHT3x initialized successfully!\n");
    
    // Bước 5: Sử dụng sensor (xem phần "Các Chế Độ Đo Lường")
    // ...
}
```

### Cấu Hình Nâng Cao
```c
// Nếu cần sử dụng I2C pins khác hoặc I2C bus khác:

// I2C Bus 1 với pins khác
sht3x_init_desc(&dev, 0x44, 1, 18, 19);  // SDA=18, SCL=19 trên I2C1

// Thay đổi tốc độ I2C (nếu hỗ trợ)
// i2cdev_set_clock(0, 400000);  // 400 kHz
```

---

## Các Chế Độ Đo Lường

SHT3x hỗ trợ 3 chế độ đo lường khác nhau:

### 1. Single Shot Mode (Đo Một Lần)
**Sử dụng khi**: Cần đo ít thường xuyên, tiết kiệm điện năng

```c
float temperature, humidity;

// Cách 1: High-level function (đơn giản)
ESP_ERROR_CHECK(sht3x_measure(&dev, &temperature, &humidity));
printf("Temperature: %.2f°C, Humidity: %.2f%%\n", temperature, humidity);

// Cách 2: Low-level functions (chi tiết hơn)
// Bước 1: Bắt đầu đo
ESP_ERROR_CHECK(sht3x_start_measurement(&dev, SHT3X_SINGLE_SHOT, SHT3X_HIGH));

// Bước 2: Chờ đo xong
uint8_t duration = sht3x_get_measurement_duration(SHT3X_HIGH);
vTaskDelay(pdMS_TO_TICKS(duration));

// Bước 3: Lấy kết quả
ESP_ERROR_CHECK(sht3x_get_results(&dev, &temperature, &humidity));
```

**Tham sốRepeatability**:
| Giá Trị | Thời Gian | Chính Xác | Dùng Cho |
|--------|----------|-----------|----------|
| `SHT3X_LOW` | 2 ms | Kém | IoT tốc độ cao |
| `SHT3X_MEDIUM` | 5 ms | Trung bình | Ứng dụng thông thường |
| `SHT3X_HIGH` | 15 ms | Cao | Đo chính xác |

### 2. Periodic Measurement Mode (Đo Định Kỳ)
**Sử dụng khi**: Cần đo thường xuyên ở tốc độ cố định

```c
// Bắt đầu đo định kỳ - 1 lần/giây
ESP_ERROR_CHECK(sht3x_start_measurement(
    &dev, 
    SHT3X_PERIODIC_1MPS,  // 1 measurement per second
    SHT3X_HIGH            // High repeatability
));

// Chờ lần đo đầu tiên hoàn tất
vTaskDelay(pdMS_TO_TICKS(sht3x_get_measurement_duration(SHT3X_HIGH)));

// Lặp để lấy dữ liệu
while (1) {
    float temperature, humidity;
    esp_err_t res = sht3x_get_results(&dev, &temperature, &humidity);
    
    if (res == ESP_OK) {
        printf("Temperature: %.2f°C, Humidity: %.2f%%\n", 
               temperature, humidity);
    } else {
        printf("Error: %s\n", esp_err_to_name(res));
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));  // Đợi 1 giây
}
```

**Tốc Độ Đo**:
- `SHT3X_PERIODIC_05MPS`: 0.5 đo/giây (2 giây/lần)
- `SHT3X_PERIODIC_1MPS`: 1 đo/giây (1 giây/lần)
- `SHT3X_PERIODIC_2MPS`: 2 đo/giây (500 ms/lần)
- `SHT3X_PERIODIC_4MPS`: 4 đo/giây (250 ms/lần)
- `SHT3X_PERIODIC_10MPS`: 10 đo/giây (100 ms/lần)

### 3. Periodic Mode với Art Detect (Phát Hiện Lỗi)
```c
// Chế độ với phát hiện lỗi
sht3x_start_measurement(
    &dev,
    SHT3X_PERIODIC_1MPS_WITH_ART,  // Art = Alert Response Time
    SHT3X_HIGH
);
```

---

## Xử Lý Lỗi

### Mã Lỗi ESP-IDF
```c
#include <esp_err.h>

esp_err_t result = sht3x_measure(&dev, &temp, &humidity);

if (result == ESP_OK) {
    printf("Đo thành công\n");
} else if (result == ESP_ERR_NOT_FOUND) {
    printf("Không tìm thấy thiết bị tại địa chỉ I2C\n");
} else if (result == ESP_ERR_TIMEOUT) {
    printf("Timeout - sensor không phản hồi\n");
} else {
    printf("Lỗi: %s (code: %d)\n", esp_err_to_name(result), result);
}
```

### Debugging I2C
```c
// Quét tất cả các thiết bị trên I2C bus
#include <i2cdev.h>

void i2c_scan() {
    printf("Scanning I2C bus...\n");
    for (uint8_t addr = 0x01; addr < 0x7f; addr++) {
        uint8_t data = 0;
        esp_err_t ret = i2c_dev_read_at(&dev, addr, NULL, 0, &data, 1);
        if (ret == ESP_OK) {
            printf("Found device at 0x%02X\n", addr);
        }
    }
}
```

### ESP_ERROR_CHECK() Macro
```c
// Cách 1: Sẽ panic nếu có lỗi
ESP_ERROR_CHECK(sht3x_init(&dev));

// Cách 2: Xử lý lỗi chi tiết
esp_err_t err = sht3x_init(&dev);
if (err != ESP_OK) {
    // Xử lý lỗi
    printf("Initialization failed: %s\n", esp_err_to_name(err));
    return;
}
```

---

## Áp Dụng cho Các Cảm Biến I2C Khác

### Mô Hình Chung (Generic Pattern)

Mọi cảm biến I2C từ esp-idf-lib tuân theo mô hình:

```c
// 1. Include header
#include <sensorname.h>  // ví dụ: <bmp280.h>, <bh1750.h>

// 2. Khai báo device structure
sensorname_t dev;

// 3. Khởi tạo I2C
i2cdev_init();

// 4. Khởi tạo device descriptor
sensorname_init_desc(&dev, 0xAA, 0, SDA_PIN, SCL_PIN);

// 5. Khởi tạo sensor
sensorname_init(&dev);

// 6. Đọc dữ liệu
sensorname_read(&dev, &data);
```

### Ví Dụ: BMP280 (Cảm Biến Áp Suất)

```c
#include <i2cdev.h>
#include <bmp280.h>

bmp280_t dev;

void app_main() {
    ESP_ERROR_CHECK(i2cdev_init());
    ESP_ERROR_CHECK(bmp280_init_desc(&dev, 0x76, 0, 21, 22));
    ESP_ERROR_CHECK(bmp280_init(&dev));
    
    float pressure, temperature, humidity;
    
    while (1) {
        ESP_ERROR_CHECK(bmp280_read_float(
            &dev,
            &temperature,
            &pressure,
            &humidity
        ));
        printf("Pressure: %.2f Pa, Temp: %.2f°C\n", pressure, temperature);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### Ví Dụ: BH1750 (Cảm Biến Ánh Sáng)

```c
#include <i2cdev.h>
#include <bh1750.h>

bh1750_t dev;

void app_main() {
    ESP_ERROR_CHECK(i2cdev_init());
    ESP_ERROR_CHECK(bh1750_init_desc(&dev, BH1750_ADDR_LO, 0, 21, 22));
    ESP_ERROR_CHECK(bh1750_setup(&dev, BH1750_MODE_ONE_TIME, BH1750_RES_HIGH));
    
    uint16_t lux;
    
    while (1) {
        if (bh1750_read(&dev, &lux) == ESP_OK) {
            printf("Light: %u lux\n", lux);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### Ví Dụ: DS3231 (Đồng Hồ Thời Gian Thực)

```c
#include <i2cdev.h>
#include <ds3231.h>

i2c_dev_t dev;
struct tm time_info;

void app_main() {
    ESP_ERROR_CHECK(i2cdev_init());
    
    // Khởi tạo
    memset(&dev, 0, sizeof(i2c_dev_t));
    dev.addr = DS3231_ADDR;
    dev.port = I2C_NUM_0;
    
    // Đặt thời gian (nếu cần)
    time_info.tm_year = 2024 - 1900;
    time_info.tm_mon = 3;
    time_info.tm_mday = 6;
    time_info.tm_hour = 12;
    time_info.tm_min = 30;
    time_info.tm_sec = 0;
    
    ESP_ERROR_CHECK(ds3231_set_time(&dev, &time_info));
    
    // Đọc thời gian
    while (1) {
        ESP_ERROR_CHECK(ds3231_get_time(&dev, &time_info));
        printf("Time: %02d:%02d:%02d\n",
               time_info.tm_hour,
               time_info.tm_min,
               time_info.tm_sec);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### Các Cảm Biến I2C Phổ Biến

| Cảm Biến | Địa Chỉ | Chức Năng | Độ Chính Xác |
|---------|--------|----------|------------|
| **SHT3x** | 0x44/0x45 | Nhiệt độ/Độ ẩm | ±0.2°C/±1.5% RH |
| **BMP280** | 0x76/0x77 | Áp suất/Độ cao | ±1 Pa |
| **DHT22** | N/A | Nhiệt độ/Độ ẩm | ±0.5°C/±2% RH |
| **BH1750** | 0x23/0x5C | Ánh sáng (lux) | ±20% |
| **DS3231** | 0x68 | Thời gian | ±2 ppm |
| **ADS1115** | 0x48-0x4B | ADC 16-bit | ±0.1% |
| **MCP23017** | 0x20-0x27 | GPIO Expander | N/A |
| **LCD1602** | 0x27/0x3F | LCD với I2C | N/A |

---

## Checklist Khi Thêm Cảm Biến I2C Mới

### Trước Khi Code
- [ ] Xác định địa chỉ I2C (datasheet)
- [ ] Xác định Pins (SDA, SCL)
- [ ] Kiểm tra điện áp cấp (3.3V hay 5V)
- [ ] Tìm library hỗ trợ (esp-idf-lib hoặc component khác)
- [ ] Kiểm tra xung nhịp I2C cần thiết

### Khi Viết Code
- [ ] Gọi `i2cdev_init()` một lần duy nhất
- [ ] Sử dụng `memset(&dev, 0, sizeof(...))` để clear structure
- [ ] Dùng `ESP_ERROR_CHECK()` hoặc xử lý lỗi
- [ ] Delay đủ theo datasheet
- [ ] Test quét I2C trước (i2c_scan)

### Debug
- [ ] Kiểm tra hàn và kết nối vật lý
- [ ] Dùng logic analyzer để xem tín hiệu I2C
- [ ] Kiểm tra pull-up resistors (4.7k ohm)
- [ ] Kiểm tra điện áp/dòng cấp

---

## Tham Khảo

### Tài Liệu Chính Thức
- [esp-idf I2C Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2c.html)
- [esp-idf-lib Repository](https://github.com/UncleRus/esp-idf-lib)
- [SHT3x Datasheet](https://sensirion.com/products/catalog/SHT30/)

### Hữu Ích Khác
- **I2C Pull-up Resistors**: Thường 4.7kΩ hoặc 10kΩ
- **I2C Bus Capacitance**: Max 400pF
- **Logic Analyzer**: Dùng để debug I2C signal
- **Oscilloscope**: Để kiểm tra timing

### I2C Tools
```bash
# Kiểm tra I2C bus (Linux)
i2cdetect -y 0

# Đọc register I2C
i2cget -y 0 0x44 0x00
```

---

## Ghi Chú Bổ Sung

### Lực Kéo (Pull-up) và Pull-down
I2C sử dụng **open-drain**, cần pull-up resistors:
```
VCC (3.3V)
    |
   [R] 4.7k ohm
    |
   SDA ---> Sensor
   
VCC (3.3V)
    |
   [R] 4.7k ohm
    |
   SCL ---> Sensor
```

### Độ Trễ (Timing) Quan Trọng
- **tBUF** (Bus Free Time): Min. 1.3 µs
- **tHD:STA** (Hold time): Min. 4 µs
- **tSU:STA** (Setup time): Min. 4.7 µs
- Trong ESP-IDF, những cái này được xử lý tự động

### Multi-Sensor I2C
```c
void app_main() {
    i2cdev_init();
    
    // Sensor 1: SHT3x
    sht3x_t sht;
    sht3x_init_desc(&sht, 0x44, 0, 21, 22);
    sht3x_init(&sht);
    
    // Sensor 2: BMP280
    bmp280_t bmp;
    bmp280_init_desc(&bmp, 0x76, 0, 21, 22);
    bmp280_init(&bmp);
    
    // Sensor 3: BH1750
    bh1750_t bh;
    bh1750_init_desc(&bh, 0x23, 0, 21, 22);
    bh1750_setup(&bh, BH1750_MODE_ONE_TIME, BH1750_RES_HIGH);
    
    // Đọc từ các sensor
    while (1) {
        float temp, humidity;
        sht3x_measure(&sht, &temp, &humidity);
        
        float pressure;
        bmp280_read_float(&bmp, &temp, &pressure, NULL);
        
        uint16_t lux;
        bh1750_read(&bh, &lux);
        
        printf("Temp: %.2f, Humidity: %.2f, Pressure: %.2f, Lux: %u\n",
               temp, humidity, pressure, lux);
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

**Tác Giả**: AI generate
