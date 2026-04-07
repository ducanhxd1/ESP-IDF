#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <bm22s2021.h>
#include <esp_log.h>

void app_main(void)
{
    bm22s2021_dev_t  smoke;
    bm22s2021_config_t cfg = {
        .uart_port  = UART_NUM_1,
        .tx_pin     = GPIO_NUM_16,   // ESP TX  → module RX
        .rx_pin     = GPIO_NUM_17,   // ESP RX  ← module TX
        .status_pin = GPIO_NUM_32,    // STATUS  ← module STATUS
    };

    ESP_ERROR_CHECK(bm22s2021_init(&smoke, &cfg));

    uint8_t data[41];
    while (1) {
        if (bm22s2021_request_info_package(&smoke, data) == ESP_OK) {
            uint16_t smoke_a = (data[17] << 8) | data[16];
            uint16_t smoke_b = (data[19] << 8) | data[18];
            ESP_LOGI("main", "Smoke A=%u  B=%u", smoke_a, smoke_b);
        }
        vTaskDelay(pdMS_TO_TICKS(8000));
    }
}