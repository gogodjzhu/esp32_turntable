/**
 * @file ui.c
 * @brief ST7789 驱动 + LVGL v9 + Spotify 播放信息显示
 *
 * 引脚与参考项目 esp32-rss-display 完全一致：
 *   MOSI=1, SCLK=0, CS=19, DC=18, RST=3, 320x240, SPI 40MHz
 */

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>

#include "lv_conf.h"
#include "lvgl.h"
#include "ui.h"

static const char *TAG = "UI";

/* ---------- ST7789 引脚与尺寸 ---------- */
#define TFT_MOSI  1
#define TFT_SCLK  0
#define TFT_CS    19
#define TFT_DC    18
#define TFT_RST   3

#define TFT_WIDTH  320
#define TFT_HEIGHT 240

#define CHUNK_HEIGHT 20
#define BUF_SIZE     (TFT_WIDTH * CHUNK_HEIGHT)
#define SPI_MAX_TRANSFER_BYTES 4092

/* ---------- 内部状态 ---------- */
static spi_device_handle_t s_spi;
static lv_display_t       *s_disp;
static lv_color_t          s_buf1[BUF_SIZE];
static lv_color_t          s_buf2[BUF_SIZE];

static uint32_t s_last_tick_ms = 0;

/* LVGL 组件引用 */
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_title_label  = NULL;
static lv_obj_t *s_artist_label = NULL;
static lv_obj_t *s_album_label  = NULL;
static lv_obj_t *s_bar          = NULL;
static lv_obj_t *s_time_label   = NULL;
static bool      s_layout_ready = false;

/* ---------- ST7789 底层 SPI 操作 ---------- */

static void tft_send_cmd(uint8_t cmd)
{
    gpio_set_level(TFT_DC, 0);
    spi_transaction_t t = {
        .length    = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void tft_send_data(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    gpio_set_level(TFT_DC, 1);
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void tft_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint8_t data[4];

    tft_send_cmd(0x2A);
    data[0] = x1 >> 8; data[1] = x1 & 0xFF;
    data[2] = x2 >> 8; data[3] = x2 & 0xFF;
    tft_send_data(data, 4);

    tft_send_cmd(0x2B);
    data[0] = y1 >> 8; data[1] = y1 & 0xFF;
    data[2] = y2 >> 8; data[3] = y2 & 0xFF;
    tft_send_data(data, 4);

    tft_send_cmd(0x2C);
}

/* ---------- LVGL flush callback ---------- */

static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w           = area->x2 - area->x1 + 1;
    uint32_t h           = area->y2 - area->y1 + 1;
    uint32_t total_bytes = w * h * 2;

    tft_set_window(area->x1, area->y1, area->x2, area->y2);

    gpio_set_level(TFT_DC, 1);
    uint8_t  *ptr       = px_map;
    uint32_t  remaining = total_bytes;
    while (remaining > 0) {
        uint32_t chunk = remaining > SPI_MAX_TRANSFER_BYTES
                         ? SPI_MAX_TRANSFER_BYTES : remaining;
        spi_transaction_t t = {
            .length    = chunk * 8,
            .tx_buffer = ptr,
        };
        spi_device_polling_transmit(s_spi, &t);
        ptr       += chunk;
        remaining -= chunk;
    }

    lv_display_flush_ready(display);
}

/* ---------- ST7789 硬件初始化 ---------- */

static void tft_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TFT_RST) | (1ULL << TFT_DC),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    gpio_set_level(TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    spi_bus_config_t buscfg = {
        .mosi_io_num    = TFT_MOSI,
        .miso_io_num    = -1,
        .sclk_io_num    = TFT_SCLK,
        .quadwp_io_num  = -1,
        .quadhd_io_num  = -1,
        .max_transfer_sz = TFT_WIDTH * CHUNK_HEIGHT * 2 + 8,
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num   = TFT_CS,
        .queue_size     = 7,
    };
    spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi);

    tft_send_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(150));
    tft_send_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));

    tft_send_cmd(0x36);
    uint8_t madctl = 0x60;
    tft_send_data(&madctl, 1);

    tft_send_cmd(0x3A);
    uint8_t pf = 0x55;
    tft_send_data(&pf, 1);

    tft_send_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(TAG, "ST7789 init done");
}

/* ---------- LVGL 初始化 ---------- */

static void lvgl_init(void)
{
    lv_init();

    s_disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, sizeof(s_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565_SWAPPED);

    ESP_LOGI(TAG, "LVGL v%d.%d.%d init, %dx%d",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
             TFT_WIDTH, TFT_HEIGHT);
}

/* ---------- 内部辅助 ---------- */

static void format_time(char *buf, size_t size, uint32_t ms)
{
    uint32_t s = ms / 1000;
    snprintf(buf, size, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

static void ui_reset_screen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    s_layout_ready = false;

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
}

/* ---------- 公开 API ---------- */

void ui_init(void)
{
    tft_init();
    lvgl_init();
    s_last_tick_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    ui_reset_screen();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *placeholder = lv_label_create(scr);
    lv_label_set_text(placeholder, "ESP32 Turntable");
    lv_obj_set_style_text_color(placeholder, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(placeholder, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "Connecting to Spotify...");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 0);

    ui_task();

    ESP_LOGI(TAG, "UI ready, heap=%ld", (long)esp_get_free_heap_size());
}

void ui_show_status(const char *msg)
{
    ui_reset_screen();

    lv_obj_t *scr = lv_screen_active();
    s_status_label = lv_label_create(scr);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status_label, TFT_WIDTH - 40);
    lv_label_set_text(s_status_label, msg);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, 0);
}

void ui_show_track(const char *title, const char *artist, const char *album,
                   uint32_t progress_ms, uint32_t duration_ms, bool is_playing)
{
    if (!s_layout_ready) {
        ui_reset_screen();

        lv_obj_t *scr = lv_screen_active();

        /* 播放状态指示 */
        s_status_label = lv_label_create(scr);
        lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 12);

        /* 曲目名（可换行，最多 3 行） */
        s_title_label = lv_label_create(scr);
        lv_label_set_long_mode(s_title_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_title_label, TFT_WIDTH - 40);
        lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 40);
        lv_obj_set_style_text_color(s_title_label, lv_color_hex(0xffffff), LV_PART_MAIN);

        /* 艺术家 */
        s_artist_label = lv_label_create(scr);
        lv_obj_align(s_artist_label, LV_ALIGN_TOP_MID, 0, 110);
        lv_obj_set_style_text_color(s_artist_label, lv_color_hex(0x9aa0a6), LV_PART_MAIN);

        /* 专辑 */
        s_album_label = lv_label_create(scr);
        lv_obj_align(s_album_label, LV_ALIGN_TOP_MID, 0, 130);
        lv_obj_set_style_text_color(s_album_label, lv_color_hex(0x6f7375), LV_PART_MAIN);

        /* 进度条 */
        s_bar = lv_bar_create(scr);
        lv_obj_set_size(s_bar, TFT_WIDTH - 40, 8);
        lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 165);
        lv_bar_set_range(s_bar, 0, 1000);
        lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x2e3a4e), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x1db954), LV_PART_INDICATOR);
        lv_obj_set_style_radius(s_bar, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(s_bar, 4, LV_PART_INDICATOR);

        /* 时间 */
        s_time_label = lv_label_create(scr);
        lv_obj_align(s_time_label, LV_ALIGN_TOP_MID, 0, 180);
        lv_obj_set_style_text_color(s_time_label, lv_color_hex(0x9aa0a6), LV_PART_MAIN);

        s_layout_ready = true;
    }

    /* 更新播放状态 */
    if (is_playing) {
        lv_label_set_text(s_status_label, "PLAYING");
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x1db954), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_status_label, "PAUSED");
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x888888), LV_PART_MAIN);
    }

    lv_label_set_text(s_title_label, title && title[0] ? title : "(no track)");
    lv_label_set_text(s_artist_label, artist && artist[0] ? artist : " ");
    lv_label_set_text(s_album_label, album && album[0] ? album : " ");

    /* 进度 */
    int value = 0;
    if (duration_ms > 0) {
        value = (int)((progress_ms * 1000ULL) / duration_ms);
        if (value > 1000) value = 1000;
    }
    lv_bar_set_value(s_bar, value, LV_ANIM_OFF);

    char timebuf[64];
    char t1[16], t2[16];
    format_time(t1, sizeof(t1), progress_ms);
    format_time(t2, sizeof(t2), duration_ms);
    snprintf(timebuf, sizeof(timebuf), "%s / %s", t1, t2);
    lv_label_set_text(s_time_label, timebuf);
}

void ui_task(void)
{
    uint32_t now_ms  = (uint32_t)(esp_timer_get_time() / 1000ULL);
    uint32_t elapsed = now_ms - s_last_tick_ms;
    if (elapsed > 0) {
        lv_tick_inc(elapsed);
        s_last_tick_ms = now_ms;
    }
    lv_timer_handler();
}
