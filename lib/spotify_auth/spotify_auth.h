#ifndef SPOTIFY_AUTH_H
#define SPOTIFY_AUTH_H

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTIFY_NVS_KEY_CLIENT_ID      "sp_client_id"
#define SPOTIFY_NVS_KEY_REFRESH_TOKEN  "sp_refresh_tok"

#define SPOTIFY_ACCESS_TOKEN_MAX  512
#define SPOTIFY_REFRESH_TOKEN_MAX 256
#define SPOTIFY_CLIENT_ID_MAX     64

/**
 * @brief Initialize Spotify auth from NVS.
 * @return ESP_OK, ESP_ERR_NOT_FOUND if not configured
 */
esp_err_t spotify_auth_init(void);

/**
 * @brief Check if refresh_token is stored in NVS.
 */
bool spotify_auth_is_configured(void);

/**
 * @brief Get a valid access token (refreshes if expired).
 * @return Pointer to access token string, or NULL on failure.
 */
const char *spotify_auth_get_token(void);

/**
 * @brief Force token refresh.
 */
esp_err_t spotify_auth_refresh(void);

#ifdef __cplusplus
}
#endif

#endif
