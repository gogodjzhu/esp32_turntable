/**
 * lv_conf.h - LVGL v9 配置 (Turntable)
 * 针对 ESP32-C3, 分块渲染, 显示 Spotify 播放信息 (含中文)
 */

#if 1

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* 颜色深度: 16bit RGB565 */
#define LV_COLOR_DEPTH 16
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 1

/* 刷新周期: ~60fps */
#define LV_DEF_REFR_PERIOD 16

/* 内存: 系统 malloc/free */
#define LV_MEM_CUSTOM 1
#include <stdlib.h>
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc

/* 日志 */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* 断言 */
#define LV_USE_ASSERT_NULL   1
#define LV_USE_ASSERT_MALLOC 1

/* HAL: esp_timer 驱动 tick */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE  "esp_timer.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR ((uint32_t)(esp_timer_get_time() / 1000ULL))

/* 字体: ASCII (Montserrat 14) + 常用中文 (Source Han Sans SC 16 CJK) */
#define LV_FONT_MONTSERRAT_8   0
#define LV_FONT_MONTSERRAT_10  0
#define LV_FONT_MONTSERRAT_12  0
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  0
#define LV_FONT_SOURCE_HAN_SANS_SC_16_CJK 1
#define LV_FONT_DEFAULT        &lv_font_source_han_sans_sc_16_cjk

/* Widget */
#define LV_USE_ARC        1
#define LV_USE_BAR        1   /* 进度条 */
#define LV_USE_LABEL      1   /* 文字 */
#define LV_USE_IMG        1
#define LV_USE_LINE       1
#define LV_USE_SPINNER    1

#define LV_USE_ANIMIMG    0
#define LV_USE_ARCLABEL   0
#define LV_USE_BUTTON     0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR   0
#define LV_USE_CANVAS     0
#define LV_USE_CHART      0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD   0
#define LV_USE_LED        0
#define LV_USE_LIST       0
#define LV_USE_MENU       0
#define LV_USE_MSGBOX     0
#define LV_USE_ROLLER     0
#define LV_USE_SCALE      0
#define LV_USE_SLIDER     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SWITCH     0
#define LV_USE_TABLE      0
#define LV_USE_TABVIEW    0
#define LV_USE_TEXTAREA   0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0

#define LV_USE_ANIM 1

/* 其他功能: 全关 */
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_PNG      0
#define LV_USE_BMP      0
#define LV_USE_SJPG     0
#define LV_USE_GIF      0
#define LV_USE_QRCODE   0
#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0
#define LV_USE_RLOTTIE  0

/* 演示/示例: 全关 */
#define LV_BUILD_EXAMPLES               0
#define LV_USE_DEMO_WIDGETS             0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER  0
#define LV_USE_DEMO_BENCHMARK           0
#define LV_USE_DEMO_STRESS              0
#define LV_USE_DEMO_MUSIC               0

/* 主题 */
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_SIMPLE  0
#define LV_USE_THEME_MONO    0

/* 布局: 全关 */
#define LV_USE_FLEX 0
#define LV_USE_GRID 0

#endif /* LV_CONF_H */
#endif /* 结束 if 1 */
