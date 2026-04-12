/*
 * tasks/task_smoke.c
 *
 * FreeRTOS task: đọc dữ liệu từ BM22S2021-1 smoke sensor,
 * đóng gói vào smoke_data_t rồi gửi vào g_smoke_queue.
 *
 * Hoạt động:
 *   - Dùng bm22s2021_is_info_available() để check non-blocking.
 *   - Nếu có packet → đọc → gửi queue.
 *   - Luôn đọc STATUS pin để detect alarm tức thì.
 *   - vTaskDelay(100ms) cuối loop để nhường CPU cho các task khác.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
 
#include <esp_log.h>
 
#include "app_config.h"
#include "share_data.h"
#include "bm22s2021.h"
 
static const char *TAG = "task_smoke";

extern bm22s2021_dev_t g_smoke_dev;

void task_smoke(void *arg) 
{
    ESP_LOGI(TAG, "start on core %d", xPortGetCoreID());
    
    uint8_t raw_data[41]; // buffer 
    smoke_data_t data;

    while (1)
    {
        if (bm22s2021_is_info_available(&g_smoke_dev)) 
        {
            bm22s2021_read_info_package(&g_smoke_dev, raw_data);

            // Parse value from package
            data.smoke_a = (uint16_t)((raw_data[17] << 8) | raw_data[16]);
            data.smoke_b = (uint16_t)((raw_data[19] << 8) | raw_data[18]);
            ESP_LOGI(TAG, "Smoke A=%u B=%u", data.smoke_a, data.smoke_b);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
}