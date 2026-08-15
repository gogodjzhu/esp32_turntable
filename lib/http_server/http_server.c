/**
 * @file http_server.c
 * @brief HTTP Server - WiFi配网 + Spotify配置 (SPIFFS静态文件)
 */

#include <string.h>
#include <stdlib.h>
#include <sys/param.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_spiffs.h>
#include "http_server.h"
#include "wifi_manager.h"
#include "nvs_manager.h"

static const char *TAG = "HTTP_SERVER";

#define SPIFFS_BASE_PATH "/www"
#define SPIFFS_PARTITION_LABEL "storage"

#define NVS_KEY_SPOTIFY_CLIENT_ID    "sp_client_id"
#define NVS_KEY_SPOTIFY_REFRESH_TOKEN "sp_refresh_tok"

static void url_decode(char *str)
{
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], 0};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static const char *get_content_type(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html; charset=UTF-8";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    return "application/octet-stream";
}

static esp_err_t serve_static_file(httpd_req_t *req, const char *filename)
{
    char filepath[64];
    snprintf(filepath, sizeof(filepath), SPIFFS_BASE_PATH "/%s", filename);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        ESP_LOGW(TAG, "File not found: %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(fsize);
    if (!buf) {
        fclose(f);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    fread(buf, 1, fsize, f);
    fclose(f);

    httpd_resp_set_type(req, get_content_type(filename));
    httpd_resp_send(req, buf, fsize);
    free(buf);
    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    wifi_info_t *info = wifi_manager_get_info();
    if (info->mode == MODE_AP) {
        return serve_static_file(req, "index.html");
    } else {
        return serve_static_file(req, "status.html");
    }
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    return serve_static_file(req, "index.html");
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char content[512];
    char ssid[32] = {0};
    char password[64] = {0};

    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';

    char *ptr = content;
    while (*ptr) {
        while (*ptr == '&') ptr++;
        if (strncmp(ptr, "ssid=", 5) == 0) {
            ptr += 5;
            char *end = strchr(ptr, '&');
            if (end) *end = '\0';
            strncpy(ssid, ptr, sizeof(ssid) - 1);
            url_decode(ssid);
            if (end) ptr = end + 1;
            else break;
        } else if (strncmp(ptr, "password=", 9) == 0) {
            ptr += 9;
            char *end = strchr(ptr, '&');
            if (end) *end = '\0';
            strncpy(password, ptr, sizeof(password) - 1);
            url_decode(password);
            break;
        } else {
            ptr++;
        }
    }

    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID is required");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saving WiFi credentials: ssid=%s", ssid);
    wifi_manager_save_credentials(ssid, password);

    serve_static_file(req, "success.html");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    wifi_info_t *info = wifi_manager_get_info();
    bool has_spotify = nvs_manager_has_key(NVS_KEY_SPOTIFY_REFRESH_TOKEN);

    char json[256];
    int len = snprintf(json, sizeof(json),
        "{\"status\":%d,\"mode\":%d,\"ip\":\"%s\",\"ssid\":\"%s\",\"ap_ssid\":\"%s\","
        "\"spotify\":%s,\"heap\":%u,\"uptime\":%u}",
        info->status,
        info->mode,
        info->ip,
        info->ssid,
        info->ap_ssid,
        has_spotify ? "true" : "false",
        (unsigned int)esp_get_free_heap_size(),
        (unsigned int)(esp_timer_get_time() / 1000000)
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, len);
    return ESP_OK;
}

static esp_err_t spotify_get_handler(httpd_req_t *req)
{
    char client_id[64] = {0};
    size_t cid_len = sizeof(client_id);
    nvs_manager_get_str(NVS_KEY_SPOTIFY_CLIENT_ID, client_id, &cid_len);

    bool has_token = nvs_manager_has_key(NVS_KEY_SPOTIFY_REFRESH_TOKEN);

    char json[128];
    int len = snprintf(json, sizeof(json),
        "{\"client_id\":\"%s\",\"has_refresh_token\":%s}",
        client_id,
        has_token ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, len);
    return ESP_OK;
}

static esp_err_t spotify_post_handler(httpd_req_t *req)
{
    char body[768] = {0};
    int ret = httpd_req_recv(req, body, sizeof(body) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    body[ret] = '\0';

    /* Parse JSON: {"client_id":"xxx","refresh_token":"yyy"} */
    char client_id[64] = {0};
    char refresh_token[256] = {0};

    const char *cid_key = "\"client_id\":\"";
    char *p = strstr(body, cid_key);
    if (p) {
        p += strlen(cid_key);
        char *end = strchr(p, '"');
        if (end && (size_t)(end - p) < sizeof(client_id)) {
            strncpy(client_id, p, end - p);
        }
    }

    const char *rt_key = "\"refresh_token\":\"";
    p = strstr(body, rt_key);
    if (p) {
        p += strlen(rt_key);
        char *end = strchr(p, '"');
        if (end && (size_t)(end - p) < sizeof(refresh_token)) {
            strncpy(refresh_token, p, end - p);
        }
    }

    if (strlen(refresh_token) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "refresh_token is required");
        return ESP_FAIL;
    }

    nvs_manager_set_str(NVS_KEY_SPOTIFY_CLIENT_ID, client_id);
    esp_err_t rt_err = nvs_manager_set_str(NVS_KEY_SPOTIFY_REFRESH_TOKEN, refresh_token);
    if (rt_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save refresh_token: %s", esp_err_to_name(rt_err));
    }

    ESP_LOGI(TAG, "Spotify config saved: client_id=%s, token_len=%d", client_id, (int)strlen(refresh_token));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", -1);

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t reconnect_get_handler(httpd_req_t *req)
{
    serve_static_file(req, "success.html");
    wifi_manager_reconnect();
    return ESP_OK;
}

static esp_err_t reset_get_handler(httpd_req_t *req)
{
    serve_static_file(req, "reset.html");
    wifi_manager_clear_credentials();
    nvs_manager_erase(NVS_KEY_SPOTIFY_CLIENT_ID);
    nvs_manager_erase(NVS_KEY_SPOTIFY_REFRESH_TOKEN);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t static_file_handler(httpd_req_t *req)
{
    const char *filename = req->uri + 1;
    if (*filename == '\0') {
        filename = "index.html";
    }
    return serve_static_file(req, filename);
}

static const httpd_uri_t root_uri = {
    .uri = "/", .method = HTTP_GET, .handler = root_get_handler,
};
static const httpd_uri_t config_uri = {
    .uri = "/config", .method = HTTP_GET, .handler = config_get_handler,
};
static const httpd_uri_t save_uri = {
    .uri = "/save", .method = HTTP_POST, .handler = save_post_handler,
};
static const httpd_uri_t status_uri = {
    .uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler,
};
static const httpd_uri_t spotify_get_uri = {
    .uri = "/api/spotify", .method = HTTP_GET, .handler = spotify_get_handler,
};
static const httpd_uri_t spotify_post_uri = {
    .uri = "/api/spotify", .method = HTTP_POST, .handler = spotify_post_handler,
};
static const httpd_uri_t reconnect_uri = {
    .uri = "/reconnect", .method = HTTP_GET, .handler = reconnect_get_handler,
};
static const httpd_uri_t reset_uri = {
    .uri = "/reset", .method = HTTP_GET, .handler = reset_get_handler,
};
static const httpd_uri_t static_file_uri = {
    .uri = "/*", .method = HTTP_GET, .handler = static_file_handler,
};

static esp_err_t spiffs_mount_storage(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE_PATH,
        .partition_label = SPIFFS_PARTITION_LABEL,
        .max_files = 5,
        .format_if_mount_failed = false,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(conf.partition_label, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: %d/%d KB used", (int)(used / 1024), (int)(total / 1024));
    return ESP_OK;
}

httpd_handle_t http_server_start(void)
{
    spiffs_mount_storage();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 10;
    config.uri_match_fn = httpd_uri_match_wildcard;
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &config_uri);
        httpd_register_uri_handler(server, &save_uri);
        httpd_register_uri_handler(server, &status_uri);
        httpd_register_uri_handler(server, &spotify_get_uri);
        httpd_register_uri_handler(server, &spotify_post_uri);
        httpd_register_uri_handler(server, &reconnect_uri);
        httpd_register_uri_handler(server, &reset_uri);
        httpd_register_uri_handler(server, &static_file_uri);
        ESP_LOGI(TAG, "HTTP server started on port 80");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }

    return server;
}

void http_server_stop(httpd_handle_t server)
{
    if (server) {
        httpd_stop(server);
    }
}
