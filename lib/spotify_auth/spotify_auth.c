#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include "spotify_auth.h"
#include "nvs_manager.h"

static const char *TAG = "SPOTIFY_AUTH";

#define TOKEN_URL "https://accounts.spotify.com/api/token"
#define TOKEN_REFRESH_MARGIN_SEC 60

static char s_access_token[SPOTIFY_ACCESS_TOKEN_MAX] = {0};
static char s_refresh_token[SPOTIFY_REFRESH_TOKEN_MAX] = {0};
static char s_client_id[SPOTIFY_CLIENT_ID_MAX] = {0};
static int64_t s_token_obtained_ms = 0;
static int s_expires_in = 0;
static bool s_initialized = false;

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

static int json_extract_str(const char *json, const char *key, char *out, size_t out_size)
{
    char search[64];
    int n = snprintf(search, sizeof(search), "\"%s\":\"", key);
    char *p = strstr(json, search);
    if (!p) return -1;
    p += n;
    char *end = strchr(p, '"');
    if (!end) return -1;
    size_t len = end - p;
    if (len >= out_size) return -1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

static int json_extract_int(const char *json, const char *key, int *out)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    char *p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    *out = atoi(p);
    return 0;
}

esp_err_t spotify_auth_init(void)
{
    if (s_initialized) return ESP_OK;

    size_t cid_len = sizeof(s_client_id);
    size_t rt_len = sizeof(s_refresh_token);

    if (nvs_manager_get_str(SPOTIFY_NVS_KEY_CLIENT_ID, s_client_id, &cid_len) != ESP_OK) {
        ESP_LOGW(TAG, "No client_id in NVS");
        return ESP_ERR_NOT_FOUND;
    }
    if (nvs_manager_get_str(SPOTIFY_NVS_KEY_REFRESH_TOKEN, s_refresh_token, &rt_len) != ESP_OK) {
        ESP_LOGW(TAG, "No refresh_token in NVS");
        return ESP_ERR_NOT_FOUND;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Initialized: client_id=%s, token_len=%d", s_client_id, (int)strlen(s_refresh_token));
    return ESP_OK;
}

bool spotify_auth_is_configured(void)
{
    return nvs_manager_has_key(SPOTIFY_NVS_KEY_REFRESH_TOKEN) &&
           nvs_manager_has_key(SPOTIFY_NVS_KEY_CLIENT_ID);
}

esp_err_t spotify_auth_refresh(void)
{
    ESP_LOGI(TAG, "Refreshing access token...");

    char body[SPOTIFY_REFRESH_TOKEN_MAX + SPOTIFY_CLIENT_ID_MAX + 64];
    int body_len = snprintf(body, sizeof(body),
        "grant_type=refresh_token&refresh_token=%s&client_id=%s",
        s_refresh_token, s_client_id);

    char resp[2048];
    resp_buf_t rb = { .buf = resp, .buf_size = sizeof(resp), .data_len = 0 };
    resp[0] = '\0';

    esp_http_client_config_t config = {
        .url = TOKEN_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &rb,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, body, body_len);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "Token refresh failed: err=%s, status=%d", esp_err_to_name(err), status);
        ESP_LOGE(TAG, "Response: %s", resp);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    esp_http_client_cleanup(client);

    if (json_extract_str(resp, "access_token", s_access_token, sizeof(s_access_token)) != 0) {
        ESP_LOGE(TAG, "Failed to parse access_token from response");
        return ESP_FAIL;
    }

    json_extract_int(resp, "expires_in", &s_expires_in);
    if (s_expires_in == 0) s_expires_in = 3600;

    /* Check if a new refresh_token was returned */
    char new_rt[SPOTIFY_REFRESH_TOKEN_MAX];
    if (json_extract_str(resp, "refresh_token", new_rt, sizeof(new_rt)) == 0) {
        strncpy(s_refresh_token, new_rt, sizeof(s_refresh_token) - 1);
        nvs_manager_set_str(SPOTIFY_NVS_KEY_REFRESH_TOKEN, s_refresh_token);
        ESP_LOGI(TAG, "Updated refresh_token in NVS");
    }

    s_token_obtained_ms = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "Token refreshed: len=%d, expires_in=%ds",
             (int)strlen(s_access_token), s_expires_in);
    return ESP_OK;
}

const char *spotify_auth_get_token(void)
{
    if (!s_initialized) {
        if (spotify_auth_init() != ESP_OK) {
            return NULL;
        }
    }

    int64_t elapsed_sec = (esp_timer_get_time() / 1000 - s_token_obtained_ms) / 1000;
    if (s_access_token[0] == '\0' || elapsed_sec >= s_expires_in - TOKEN_REFRESH_MARGIN_SEC) {
        if (spotify_auth_refresh() != ESP_OK) {
            return NULL;
        }
    }

    return s_access_token;
}
