
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
 
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <driver/i2c.h>

#include "sht3x.h"
#include "app_config.h"
#include "share_data.h"
 
static const char *TAG = "task_sht31";
extern sht3x_t dev; // driver i2c


void task_sht31(void *arg) 
{
    ESP_LOGI(TAG, "started on core %d", xPortGetCoreID());
    sht31_data_t data;    
    TickType_t last_wakeup = xTaskGetTickCount();

    while (1)
    {
        // perform one measurement and do something with the results
        ESP_ERROR_CHECK(sht3x_measure(&dev, &data.temperature, &data.humidity));
        ESP_LOGI(TAG, "SHT3x Sensor: %.2f °C, %.2f %%", data.temperature, data.humidity);

        // wait until 5 seconds are over
        vTaskDelayUntil(&last_wakeup, pdMS_TO_TICKS(5000));
    }
}