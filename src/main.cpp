#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_manager.h"
#include "wifi_manager.h"
#include "http_server.h"
#include "sntp_sync.h"
#include "spotify_auth.h"
#include "spotify_client.h"
#include "spotify_device.h"

static const char *TAG = "main";

static void print_help(void)
{
    printf("\n=== Spotify Remote Control ===\n");
    printf("Commands:\n");
    printf("  d  - List devices\n");
    printf("  s  - Show playback state\n");
    printf("  f  - Find iPhone\n");
    printf("  t  - Transfer playback to iPhone\n");
    printf("  SPC- Play / Pause (toggle)\n");
    printf("  n  - Next track\n");
    printf("  b  - Previous track\n");
    printf("  h  - Help\n\n");
}

static void cmd_devices(void)
{
    char buf[2048];
    int status = 0;
    esp_err_t err = spotify_client_get_devices(buf, sizeof(buf), &status);
    printf("Devices (HTTP %d, err=%s):\n%s\n", status, esp_err_to_name(err), buf);
}

static void cmd_state(void)
{
    char buf[2048];
    int status = 0;
    spotify_client_get_state(buf, sizeof(buf), &status);
    if (status == 204) {
        printf("No playback active.\n");
    } else {
        printf("State (HTTP %d):\n%s\n", status, buf);
    }
}

static void cmd_find(void)
{
    if (spotify_device_find() == ESP_OK) {
        printf("Device found: id=%s\n", spotify_device_get_id());
    } else {
        printf("No smartphone device found. Open Spotify app on iPhone.\n");
    }
}

static void cmd_transfer(void)
{
    const char *id = spotify_device_get_id();
    if (!id) { printf("No device. Press 'f' first.\n"); return; }
    int st = 0;
    spotify_client_transfer_playback(id, false, &st);
    printf("Transfer: HTTP %d\n", st);
}

static void cmd_toggle(void)
{
    const char *id = spotify_device_get_id();
    if (!id) { printf("No device. Press 'f' first.\n"); return; }

    bool playing = false;
    if (spotify_client_is_playing(&playing) != ESP_OK) {
        printf("Failed to get playback state.\n");
        return;
    }

    int st = 0;
    if (playing) {
        spotify_client_pause(id, &st);
        printf("Pause: HTTP %d\n", st);
    } else {
        spotify_client_play(id, NULL, &st);
        printf("Play: HTTP %d\n", st);
    }
}

static void cmd_next(void)
{
    const char *id = spotify_device_get_id();
    int st = 0;
    spotify_client_next(id, &st);
    printf("Next: HTTP %d\n", st);
}

static void cmd_previous(void)
{
    const char *id = spotify_device_get_id();
    int st = 0;
    spotify_client_previous(id, &st);
    printf("Previous: HTTP %d\n", st);
}

static void spotify_task(void *arg)
{
    while (wifi_manager_get_info()->status != WIFI_STATUS_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "WiFi connected, starting Spotify...");

    if (sntp_sync_init(15000) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync timeout, HTTPS may fail");
    }

    if (!spotify_auth_is_configured()) {
        ESP_LOGW(TAG, "Spotify not configured!");
        printf("Open http://%s to configure Spotify credentials.\n",
               wifi_manager_get_info()->ip);
        vTaskDelete(NULL);
    }

    if (spotify_auth_init() != ESP_OK) {
        ESP_LOGE(TAG, "Spotify auth init failed");
        vTaskDelete(NULL);
    }

    printf("\n=== Initial Verification ===\n");
    cmd_devices();
    cmd_find();
    cmd_state();
    print_help();

    /* Set stdin non-blocking so we can yield to IDLE task */
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    while (true) {
        int c = getchar();
        if (c >= 0) {
            switch (c) {
                case 'd': cmd_devices(); break;
                case 's': cmd_state(); break;
                case 'f': cmd_find(); break;
                case 't': cmd_transfer(); break;
                case ' ': cmd_toggle(); break;
                case 'n': cmd_next(); break;
                case 'b': cmd_previous(); break;
                case 'h': print_help(); break;
                default: break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Turntable starting...");

    wifi_manager_init();
    esp_err_t ret = wifi_manager_smart_connect();

    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "AP config mode: connect to '%s' and open http://192.168.4.1",
                 wifi_manager_get_info()->ap_ssid);
        http_server_start();
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "WiFi: %s, IP: %s",
             wifi_manager_get_info()->ssid,
             wifi_manager_get_info()->ip);

    http_server_start();
    xTaskCreate(spotify_task, "spotify", 16384, NULL, 5, NULL);
}
