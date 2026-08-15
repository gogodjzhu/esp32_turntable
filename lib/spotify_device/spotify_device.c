#include <string.h>
#include <esp_log.h>
#include "spotify_device.h"
#include "spotify_client.h"

static const char *TAG = "SPOTIFY_DEVICE";

static char s_device_id[SPOTIFY_DEVICE_ID_MAX] = {0};
static char s_device_name[32] = {0};
static bool s_cached = false;

/**
 * Skip whitespace characters.
 */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/**
 * Given a position in JSON, search backwards for "key" and extract its string value.
 * Handles spaces around colons: "key" : "value"
 */
static bool extract_value_before(const char *json, const char *end_pos,
                                 const char *key, char *out, size_t out_size)
{
    char key_search[32];
    int key_len = snprintf(key_search, sizeof(key_search), "\"%s\"", key);

    const char *p = end_pos - 1;
    while (p >= json) {
        if (strncmp(p, key_search, key_len) == 0) {
            const char *v = skip_ws(p + key_len);
            if (*v == ':') v++;
            v = skip_ws(v);
            if (*v == '"') v++;
            const char *vend = strchr(v, '"');
            if (vend && (size_t)(vend - v) < out_size) {
                memcpy(out, v, vend - v);
                out[vend - v] = '\0';
                return true;
            }
        }
        p--;
    }
    return false;
}

esp_err_t spotify_device_find(void)
{
    char resp[2048];
    int status = 0;

    esp_err_t err = spotify_client_get_devices(resp, sizeof(resp), &status);
    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "Failed to get devices: err=%s, status=%d", esp_err_to_name(err), status);
        return ESP_FAIL;
    }

    /* Search for "Smartphone" in the response (handles spaces around colon) */
    const char *type_pos = strstr(resp, "Smartphone");
    if (!type_pos) {
        type_pos = strstr(resp, "smartphone");
    }

    if (!type_pos) {
        ESP_LOGW(TAG, "No smartphone device found");
        s_cached = false;
        s_device_id[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }

    if (!extract_value_before(resp, type_pos, "id", s_device_id, sizeof(s_device_id))) {
        ESP_LOGE(TAG, "Found smartphone but couldn't extract device_id");
        return ESP_FAIL;
    }

    s_device_name[0] = '\0';
    extract_value_before(resp, type_pos, "name", s_device_name, sizeof(s_device_name));

    s_cached = true;
    ESP_LOGI(TAG, "Found device: name=%s, id=%s", s_device_name, s_device_id);
    return ESP_OK;
}

const char *spotify_device_get_id(void)
{
    if (!s_cached) {
        if (spotify_device_find() != ESP_OK) {
            return NULL;
        }
    }
    return s_device_id;
}

void spotify_device_invalidate(void)
{
    s_cached = false;
    s_device_id[0] = '\0';
}
