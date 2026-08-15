#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "heartbeat demo started");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        ESP_LOGI(TAG, "heartbeat");
    }
}
