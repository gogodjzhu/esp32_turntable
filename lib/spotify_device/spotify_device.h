#ifndef SPOTIFY_DEVICE_H
#define SPOTIFY_DEVICE_H

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTIFY_DEVICE_ID_MAX 64

/**
 * @brief Find iPhone (smartphone) among available devices.
 * Caches the device_id internally.
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND if no smartphone device
 */
esp_err_t spotify_device_find(void);

/**
 * @brief Get cached device_id. Calls spotify_device_find() if not cached.
 * @return Pointer to device_id string, or NULL if not found.
 */
const char *spotify_device_get_id(void);

/**
 * @brief Clear cached device_id (force re-discovery on next get_id call).
 */
void spotify_device_invalidate(void);

#ifdef __cplusplus
}
#endif

#endif
