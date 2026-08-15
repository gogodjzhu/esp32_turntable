#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include "sntp_sync.h"

static const char *TAG = "SNTP";
static bool s_synced = false;

static void sntp_callback(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized");
    s_synced = true;
}

esp_err_t sntp_sync_init(uint32_t timeout_ms)
{
    if (s_synced) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    sntp_set_time_sync_notification_cb(sntp_callback);
    esp_sntp_init();

    if (timeout_ms == 0) {
        return ESP_OK;
    }

    uint32_t elapsed = 0;
    while (!s_synced && elapsed < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(500));
        elapsed += 500;
    }

    if (s_synced) {
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        ESP_LOGI(TAG, "Current time: %s", strftime_buf);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "SNTP sync timeout after %d ms", timeout_ms);
    return ESP_ERR_TIMEOUT;
}

bool sntp_sync_is_synced(void)
{
    return s_synced;
}
