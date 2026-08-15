#ifndef INPUT_H
#define INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INPUT_NONE = 0,
    INPUT_SHORT_PRESS,    /* 1 次点击 */
    INPUT_DOUBLE_PRESS,   /* 2 次点击 */
    INPUT_TRIPLE_PRESS,   /* 3 次点击 */
} input_event_t;

/**
 * @brief 初始化按钮 (GPIO9, 内部上拉, 下降沿中断)。
 * 需要在启动时调用一次。
 */
void input_init(void);

/**
 * @brief 轮询按钮手势。主循环周期性调用。
 * @return 手势事件，无事件时返回 INPUT_NONE
 */
input_event_t input_poll(void);

#ifdef __cplusplus
}
#endif

#endif
