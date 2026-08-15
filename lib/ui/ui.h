#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化显示屏 (ST7789) + LVGL。启动时调用一次。
 */
void ui_init(void);

/**
 * @brief 显示当前播放曲目信息。
 * @param title       曲目名（可为空字符串）
 * @param artist      艺术家（可为空字符串）
 * @param album       专辑名（可为空字符串）
 * @param progress_ms 当前播放进度（毫秒）
 * @param duration_ms 曲目总时长（毫秒）
 * @param is_playing  是否正在播放
 */
void ui_show_track(const char *title, const char *artist, const char *album,
                   uint32_t progress_ms, uint32_t duration_ms, bool is_playing);

/**
 * @brief 显示纯状态文字（如无设备、连接中、出错）。
 * @param msg 要显示的消息
 */
void ui_show_status(const char *msg);

/**
 * @brief 驱动 LVGL 渲染。需周期性调用（主循环或独立 task）。
 */
void ui_task(void);

#ifdef __cplusplus
}
#endif

#endif
