/**
 * @file rbg_led.h
 * @brief 声明 WS2812B 灯带效果和运行时参数接口。
 *
 * 硬件：8 颗 WS2812B LED，DIN 连接 GPIO 8。
 */

#ifndef RBG_LED_H
#define RBG_LED_H

#include <Arduino.h>

#define RBG_LED_PIN         8
#define RBG_LED_COUNT       8
#define RBG_LED_BRIGHTNESS  40

/** @brief 固件内置灯效编号，数值会写入设备设置。 */
enum RbgLedEffect : uint8_t {
    RBG_EFFECT_RAINBOW = 0,
    RBG_EFFECT_BREATHING,
    RBG_EFFECT_COMET,
    RBG_EFFECT_COLOR_WIPE,
    RBG_EFFECT_FIRE,
    RBG_EFFECT_SOLID_FADE,
    RBG_EFFECT_SOLID_COLOR,
    RBG_EFFECT_RAINBOW_BREATHING,
    RBG_EFFECT_COUNT,
};

/** @brief 初始化 GPIO、灯珠缓冲区和默认效果。 */
void rbgLedInit();
/** @brief 非阻塞推进当前灯效；应在 loop() 中频繁调用。 */
void rbgLedUpdate();
/** @brief 切换到下一种内置灯效。 */
void rbgLedNextEffect();
/** @brief 设置灯效编号；越界值会被规范化。 */
void rbgLedSetEffect(uint8_t effect);
uint8_t rbgLedGetEffect();
const char *rbgLedGetEffectName();
/** @brief 设置全局亮度百分比，内部限制在安全范围。 */
void rbgLedSetBrightness(uint8_t percent);
uint8_t rbgLedGetBrightness();
/** @brief 设置动画速度百分比。 */
void rbgLedSetSpeed(uint8_t percent);
uint8_t rbgLedGetSpeed();
/** @brief 设置常亮模式使用的 RGB 颜色。 */
void rbgLedSetColor(uint8_t red, uint8_t green, uint8_t blue);
/** @brief 总开关；关闭后立即清空灯珠。 */
void rbgLedSetEnabled(bool enabled);
bool rbgLedIsEnabled();

#endif /* RBG_LED_H 结束 */
