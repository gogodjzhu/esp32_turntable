#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_manager.h"
#include "wifi_manager.h"
#include "http_server.h"
#include "sntp_sync.h"
#include "spotify_auth.h"
#include "spotify_client.h"
#include "spotify_device.h"
#include "ui.h"
#include "input.h"

static const char *TAG = "main";

#define DISPLAY_REFRESH_MS 2000

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
    printf("  h  - Help\n");
    printf("Button (GPIO2):\n");
    printf("  short = toggle,  double = next,  triple = previous\n\n");
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

static void refresh_display(void)
{
    const char *id = spotify_device_get_id();
    if (!id) {
        ui_show_status("No device.\nOpen Spotify app on iPhone.");
        return;
    }

    spotify_playback_t pb;
    if (spotify_client_get_playback(&pb) != ESP_OK) {
        ui_show_status("Failed to get playback state.");
        return;
    }

    if (!pb.has_track) {
        ui_show_status("No track playing.");
        return;
    }

    ui_show_track(pb.title, pb.artist, "", pb.progress_ms, pb.duration_ms, pb.is_playing);
}

static void spotify_task(void *arg)
{
    ui_show_status("Connecting WiFi...");
    while (wifi_manager_get_info()->status != WIFI_STATUS_CONNECTED) {
        ui_task();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGI(TAG, "WiFi connected, starting Spotify...");

    ui_show_status("Syncing time...");
    if (sntp_sync_init(15000) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync timeout, HTTPS may fail");
    }

    if (!spotify_auth_is_configured()) {
        ESP_LOGW(TAG, "Spotify not configured!");
        char msg[96];
        snprintf(msg, sizeof(msg), "Spotify not configured.\nOpen http://%s", wifi_manager_get_info()->ip);
        ui_show_status(msg);
        ui_task();
        printf("Open http://%s to configure Spotify credentials.\n",
               wifi_manager_get_info()->ip);
        vTaskDelete(NULL);
    }

    if (spotify_auth_init() != ESP_OK) {
        ESP_LOGE(TAG, "Spotify auth init failed");
        ui_show_status("Spotify auth init failed.");
        ui_task();
        vTaskDelete(NULL);
    }

    printf("\n=== Initial Verification ===\n");
    cmd_devices();
    cmd_find();
    cmd_state();
    print_help();

    /* 初始化按钮 (GPIO9) */
    input_init();

    /* Set stdin non-blocking so we can yield to IDLE task */
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    uint32_t last_refresh_ms = 0;

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

        /* 按钮手势：短按=播放/暂停, 双击=下一首, 三击=上一首 */
        input_event_t ev = input_poll();
        switch (ev) {
            case INPUT_SHORT_PRESS:  cmd_toggle(); break;
            case INPUT_DOUBLE_PRESS: cmd_next(); break;
            case INPUT_TRIPLE_PRESS: cmd_previous(); break;
            default: break;
        }

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if (now_ms - last_refresh_ms >= DISPLAY_REFRESH_MS) {
            last_refresh_ms = now_ms;
            refresh_display();
        }
        ui_task();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Turntable starting...");

    ui_init();
    ui_show_status("Booting...");
    ui_task();

    wifi_manager_init();
    esp_err_t ret = wifi_manager_smart_connect();

    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "AP config mode: connect to '%s' and open http://192.168.4.1",
                 wifi_manager_get_info()->ap_ssid);
        char msg[96];
        snprintf(msg, sizeof(msg), "Config mode.\nConnect WiFi '%s' then open\nhttp://192.168.4.1",
                 wifi_manager_get_info()->ap_ssid);
        ui_show_status(msg);
        http_server_start();
        while (true) {
            ui_task();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    ESP_LOGI(TAG, "WiFi: %s, IP: %s",
             wifi_manager_get_info()->ssid,
             wifi_manager_get_info()->ip);

    http_server_start();
    xTaskCreate(spotify_task, "spotify", 16384, NULL, 5, NULL);
}
