#include <string.h>
#include <stdio.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include "spotify_client.h"
#include "spotify_auth.h"

static const char *TAG = "SPOTIFY_CLIENT";

#define SPOTIFY_API_BASE "https://api.spotify.com/v1"

typedef struct {
    char *buf;
    size_t buf_size;
    size_t data_len;
} resp_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    resp_buf_t *rb = evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && rb && rb->buf) {
        int copy = evt->data_len;
        if (rb->data_len + copy >= rb->buf_size) {
            copy = rb->buf_size - 1 - rb->data_len;
        }
        if (copy > 0) {
            memcpy(rb->buf + rb->data_len, evt->data, copy);
            rb->data_len += copy;
            rb->buf[rb->data_len] = '\0';
        }
    }
    return ESP_OK;
}

static esp_err_t spotify_request(
    esp_http_client_method_t method,
    const char *path,
    const char *body,
    char *resp_buf,
    size_t resp_buf_size,
    int *status_code)
{
    const char *token = spotify_auth_get_token();
    if (!token) {
        ESP_LOGE(TAG, "No access token");
        return ESP_FAIL;
    }

    char url[512];
    snprintf(url, sizeof(url), "%s%s", SPOTIFY_API_BASE, path);

    char fallback_buf[256];
    char *eff_buf = resp_buf;
    size_t eff_size = resp_buf_size;
    if (!eff_buf) {
        eff_buf = fallback_buf;
        eff_size = sizeof(fallback_buf);
    }

    resp_buf_t rb = { .buf = eff_buf, .buf_size = eff_size, .data_len = 0 };
    eff_buf[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .event_handler = http_event_handler,
        .user_data = &rb,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    char auth_header[SPOTIFY_ACCESS_TOKEN_MAX + 16];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth_header);

    if (body) {
        esp_http_client_set_post_field(client, body, strlen(body));
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (status_code) *status_code = status;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Request failed: %s, err=%s", path, esp_err_to_name(err));
    } else if (status >= 400) {
        ESP_LOGW(TAG, "%s -> HTTP %d: %s", path, status, eff_buf);
    } else {
        ESP_LOGI(TAG, "%s -> HTTP %d", path, status);
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t spotify_client_get_devices(char *buf, size_t buf_size, int *status_code)
{
    return spotify_request(HTTP_METHOD_GET, "/me/player/devices", NULL, buf, buf_size, status_code);
}

esp_err_t spotify_client_get_state(char *buf, size_t buf_size, int *status_code)
{
    return spotify_request(HTTP_METHOD_GET, "/me/player", NULL, buf, buf_size, status_code);
}

esp_err_t spotify_client_is_playing(bool *is_playing)
{
    char buf[2048];
    int status = 0;
    esp_err_t err = spotify_client_get_state(buf, sizeof(buf), &status);
    if (err != ESP_OK) return err;
    if (status == 204) {
        *is_playing = false;
        return ESP_OK;
    }
    if (status != 200) return ESP_FAIL;

    const char *key = "\"is_playing\"";
    const char *p = strstr(buf, key);
    if (!p) return ESP_FAIL;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    if (*p == ':') p++;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    *is_playing = (strncmp(p, "true", 4) == 0);
    return ESP_OK;
}

esp_err_t spotify_client_transfer_playback(const char *device_id, bool play, int *status_code)
{
    char body[128];
    snprintf(body, sizeof(body), "{\"device_ids\":[\"%s\"],\"play\":%s}",
             device_id, play ? "true" : "false");
    return spotify_request(HTTP_METHOD_PUT, "/me/player", body, NULL, 0, status_code);
}

esp_err_t spotify_client_play(const char *device_id, const char *context_uri, int *status_code)
{
    char path[128];
    if (device_id) {
        snprintf(path, sizeof(path), "/me/player/play?device_id=%s", device_id);
    } else {
        strcpy(path, "/me/player/play");
    }

    if (context_uri) {
        char body[256];
        snprintf(body, sizeof(body), "{\"context_uri\":\"%s\"}", context_uri);
        return spotify_request(HTTP_METHOD_PUT, path, body, NULL, 0, status_code);
    }
    return spotify_request(HTTP_METHOD_PUT, path, NULL, NULL, 0, status_code);
}

esp_err_t spotify_client_play_track(const char *device_id, const char *track_uri, int *status_code)
{
    char path[128];
    if (device_id) {
        snprintf(path, sizeof(path), "/me/player/play?device_id=%s", device_id);
    } else {
        strcpy(path, "/me/player/play");
    }

    char body[256];
    snprintf(body, sizeof(body), "{\"uris\":[\"%s\"]}", track_uri);
    return spotify_request(HTTP_METHOD_PUT, path, body, NULL, 0, status_code);
}

esp_err_t spotify_client_pause(const char *device_id, int *status_code)
{
    char path[128];
    if (device_id) {
        snprintf(path, sizeof(path), "/me/player/pause?device_id=%s", device_id);
    } else {
        strcpy(path, "/me/player/pause");
    }
    return spotify_request(HTTP_METHOD_PUT, path, NULL, NULL, 0, status_code);
}

esp_err_t spotify_client_next(const char *device_id, int *status_code)
{
    char path[128];
    if (device_id) {
        snprintf(path, sizeof(path), "/me/player/next?device_id=%s", device_id);
    } else {
        strcpy(path, "/me/player/next");
    }
    return spotify_request(HTTP_METHOD_POST, path, NULL, NULL, 0, status_code);
}

esp_err_t spotify_client_previous(const char *device_id, int *status_code)
{
    char path[128];
    if (device_id) {
        snprintf(path, sizeof(path), "/me/player/previous?device_id=%s", device_id);
    } else {
        strcpy(path, "/me/player/previous");
    }
    return spotify_request(HTTP_METHOD_POST, path, NULL, NULL, 0, status_code);
}
