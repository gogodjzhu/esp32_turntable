#include <string.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "input.h"

static const char *TAG = "INPUT";

#define BTN_GPIO           GPIO_NUM_9
#define BTN_DEBOUNCE_US    50000LL    /* 50ms 防抖 */
#define GESTURE_TIMEOUT_MS 350        /* 最后一次点击后等待判定手势 */

static QueueHandle_t s_btn_queue = NULL;
static int           s_click_count = 0;
static int64_t       s_last_click_ms = 0;

/* GPIO9 下降沿中断，50ms 软件防抖，时间戳入队 */
static void IRAM_ATTR gpio_button_isr(void *arg)
{
    static int64_t last_us = 0;
    int64_t now = esp_timer_get_time();
    if (now - last_us < BTN_DEBOUNCE_US) return;
    last_us = now;

    BaseType_t woken = pdFALSE;
    if (s_btn_queue != NULL) {
        xQueueSendFromISR(s_btn_queue, &now, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

void input_init(void)
{
    if (s_btn_queue != NULL) {
        return; /* 已初始化 */
    }

    s_btn_queue = xQueueCreate(4, sizeof(int64_t));

    gpio_config_t cfg = {
        .pin_bit_mask  = (1ULL << BTN_GPIO),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&cfg);

    /* ISR 服务可能已被其他组件安装，忽略重复安装错误 */
    if (gpio_install_isr_service(0) != ESP_OK) {
        ESP_LOGW(TAG, "ISR service already installed");
    }
    gpio_isr_handler_add(BTN_GPIO, gpio_button_isr, NULL);

    s_click_count = 0;
    ESP_LOGI(TAG, "Button GPIO9 initialized");
}

input_event_t input_poll(void)
{
    /* 收集队列中的按下事件 */
    int64_t ts;
    bool pressed = false;
    while (xQueueReceive(s_btn_queue, &ts, 0) == pdTRUE) {
        pressed = true;
        /* 只更新最新的点击时间戳（合并快速连按） */
    }

    if (pressed) {
        s_click_count++;
        s_last_click_ms = esp_timer_get_time() / 1000;
    }

    if (s_click_count > 0) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - s_last_click_ms >= GESTURE_TIMEOUT_MS) {
            input_event_t ev;
            switch (s_click_count) {
                case 1:  ev = INPUT_SHORT_PRESS;  break;
                case 2:  ev = INPUT_DOUBLE_PRESS; break;
                default: ev = INPUT_TRIPLE_PRESS; break;  /* >=3 视为三击 */
            }
            s_click_count = 0;
            return ev;
        }
    }

    return INPUT_NONE;
}
