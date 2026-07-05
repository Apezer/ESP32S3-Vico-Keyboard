/**
 * rgb_led.h - RGB 灯带控制
 *
 * WS2812B 灯带多种灯效
 */

#ifndef RGB_LED_H
#define RGB_LED_H

#include <Arduino.h>
#include <FastLED_min.h>

// ===== LED 配置 =====
#define LED_PIN         17
#define NUM_LEDS        8
#define LED_BRIGHTNESS  40

// ===== 灯效枚举 =====
enum LedEffect {
    EFFECT_RAINBOW = 0,     // 彩虹流水
    EFFECT_BREATHING,       // 呼吸灯
    EFFECT_COMET,           // 流星
    EFFECT_COLOR_WIPE,      // 逐个填充
    EFFECT_FIRE,            // 火焰
    EFFECT_SOLID,           // 纯色
    EFFECT_COUNT            // 灯效总数
};

// ===== 全局 LED 数组 =====
extern CRGB leds[NUM_LEDS];

// ===== 函数声明 =====
void rgbLedInit();
void rgbLedUpdate();
void rgbLedNextEffect();
void rgbLedSetEffect(uint8_t effect);
const char *rgbLedGetEffectName();

#endif /* RGB_LED_H */
