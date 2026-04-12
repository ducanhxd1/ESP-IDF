#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
 
#include <esp_log.h>
#include <esp_err.h>
 
/* Config phần cứng tập trung */
#include "app_config.h"
 
/* Struct dữ liệu chia sẻ */
#include "share_data.h"
 
/* Driver components */
#include "bm22s2021.h"
#include "sht3x.h"

static const char *TAG = "app_main";
bm22s2021_dev_t g_smoke_dev;
sht3x_t dev;

extern void task_smoke(void *arg);
extern void task_sht31(void *arg);


void app_main(void)
{
    ESP_LOGI(TAG, "=== Booting my_project ===");

    bm22s2021_config_t smoke_cfg = {
        .uart_port  = SMOKE_UART_PORT,
        .tx_pin     = SMOKE_TX_PIN,
        .rx_pin     = SMOKE_RX_PIN,
        .status_pin = SMOKE_STATUS_PIN,
    };
 
    ESP_ERROR_CHECK(bm22s2021_init(&g_smoke_dev, &smoke_cfg));
    ESP_LOGI(TAG, "BM22S2021 OK");

    ESP_ERROR_CHECK(i2cdev_init());
    memset(&dev, 0, sizeof(sht3x_t));

    ESP_ERROR_CHECK(sht3x_init_desc(&dev, CONFIG_EXAMPLE_SHT3X_ADDR, 0, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));
    ESP_ERROR_CHECK(sht3x_init(&dev));
    ESP_LOGI(TAG, "SHT3x OK");
    vTaskDelay(pdMS_TO_TICKS(100));

    // Create task
    xTaskCreatePinnedToCore(
        task_smoke,
        "task_smoke",
        2048,
        NULL,
        5,
        NULL,
        0
    );

    xTaskCreatePinnedToCore(
        task_sht31,
        "task_sht31",
        2048,
        NULL,
        5,
        NULL,
        0
    );

    ESP_LOGI(TAG, "All tasks spawned. Scheduler running.");
}