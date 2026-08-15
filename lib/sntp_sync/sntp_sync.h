#ifndef SNTP_SYNC_H
#define SNTP_SYNC_H

#include <esp_err.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SNTP and wait for time sync.
 * @param timeout_ms Maximum time to wait for sync (0 = non-blocking)
 * @return ESP_OK if synced, ESP_ERR_TIMEOUT if not synced in time
 */
esp_err_t sntp_sync_init(uint32_t timeout_ms);

/**
 * @brief Check if system time is valid (after SNTP sync).
 */
bool sntp_sync_is_synced(void);

#ifdef __cplusplus
}
#endif

#endif
