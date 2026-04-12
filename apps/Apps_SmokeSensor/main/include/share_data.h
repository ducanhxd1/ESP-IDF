#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t smoke_a;   // giá trị kênh A
    uint16_t smoke_b;   // giá trị kênh B
    uint8_t  alarm;     // 0=bình thường, 1=báo động
} smoke_data_t;

typedef struct {
    
    float temperature;  // °C
    float humidity;     // %RH
} sht31_data_t;