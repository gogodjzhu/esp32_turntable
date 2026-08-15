#ifndef SPOTIFY_CLIENT_H
#define SPOTIFY_CLIENT_H

#include <esp_err.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 解析后的播放状态结构体。
 */
typedef struct {
    char title[128];      /**< 曲目名 */
    char artist[128];     /**< 艺术家 */
    bool is_playing;      /**< 是否播放中 */
    bool has_track;       /**< 是否有当前曲目（item 非 null） */
    uint32_t progress_ms; /**< 当前进度（毫秒） */
    uint32_t duration_ms; /**< 总时长（毫秒） */
} spotify_playback_t;

/**
 * @brief Get list of available devices.
 * @param buf Output buffer for JSON response
 * @param buf_size Buffer size
 * @param status_code Output HTTP status code (can be NULL)
 * @return ESP_OK on success
 */
esp_err_t spotify_client_get_devices(char *buf, size_t buf_size, int *status_code);

/**
 * @brief Get current playback state.
 */
esp_err_t spotify_client_get_state(char *buf, size_t buf_size, int *status_code);

/**
 * @brief Check if playback is currently active.
 * @param is_playing Output: true if playing, false if paused/stopped
 */
esp_err_t spotify_client_is_playing(bool *is_playing);

/**
 * @brief 获取当前播放状态并解析到结构体。
 * @param out 输出结构体
 */
esp_err_t spotify_client_get_playback(spotify_playback_t *out);

/**
 * @brief Transfer playback to a device.
 */
esp_err_t spotify_client_transfer_playback(const char *device_id, bool play, int *status_code);

/**
 * @brief Start/Resume playback. If context_uri is NULL, just resume.
 */
esp_err_t spotify_client_play(const char *device_id, const char *context_uri, int *status_code);

/**
 * @brief Play a specific track.
 */
esp_err_t spotify_client_play_track(const char *device_id, const char *track_uri, int *status_code);

/**
 * @brief Pause playback.
 */
esp_err_t spotify_client_pause(const char *device_id, int *status_code);

/**
 * @brief Skip to next track.
 */
esp_err_t spotify_client_next(const char *device_id, int *status_code);

/**
 * @brief Skip to previous track.
 */
esp_err_t spotify_client_previous(const char *device_id, int *status_code);

#ifdef __cplusplus
}
#endif

#endif
