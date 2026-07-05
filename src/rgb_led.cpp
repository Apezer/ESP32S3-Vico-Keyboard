/**
 * rgb_led.cpp - RGB 灯带控制
 *
 * WS2812B 灯带多种灯效实现
 */

#include "rgb_led.h"

// ===== 全局变量 =====
CRGB leds[NUM_LEDS];
static uint8_t current_effect = EFFECT_RAINBOW;

// ===== 灯效名称 =====
static const char *EFFECT_NAMES[] = {
    "Rainbow",
    "Breathing",
    "Comet",
    "Wipe",
    "Fire",
    "Solid",
};

// ===== 辅助函数: HSV 转 RGB =====
static CRGB hsvToRgb(uint8_t h, uint8_t s, uint8_t v) {
    uint8_t region = h / 43;
    uint8_t remainder = (h - (region * 43)) * 6;
    uint8_t p = (v * (255 - s)) / 255;
    uint8_t q = (v * (255 - ((s * remainder) / 255))) / 255;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) / 255))) / 255;

    switch (region) {
        case 0:  return CRGB(v, t, p);
        case 1:  return CRGB(q, v, p);
        case 2:  return CRGB(p, v, t);
        case 3:  return CRGB(p, q, v);
        case 4:  return CRGB(t, p, v);
        default: return CRGB(v, p, q);
    }
}

// ===== 灯效1: 彩虹流水 =====
static void effectRainbow() {
    static uint8_t hue_offset = 0;
    static unsigned long last_ms = 0;

    if (millis() - last_ms < 30) return;
    last_ms = millis();

    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        leds[i] = hsvToRgb(hue_offset + i * 32, 255, LED_BRIGHTNESS);
    }
    hue_offset += 3;
    FastLED_min<LED_PIN>.show();
}

// ===== 灯效2: 呼吸灯 =====
static void effectBreathing() {
    static uint8_t brightness = 0;
    static int8_t direction = 1;
    static unsigned long last_ms = 0;

    if (millis() - last_ms < 15) return;
    last_ms = millis();

    brightness += direction * 2;
    if (brightness >= LED_BRIGHTNESS) direction = -1;
    if (brightness <= 0) direction = 1;

    CRGB color = CRGB(brightness, 0, brightness / 2);
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        leds[i] = color;
    }
    FastLED_min<LED_PIN>.show();
}

// ===== 灯效3: 流星 =====
static void effectComet() {
    static uint8_t pos = 0;
    static uint8_t hue = 0;
    static unsigned long last_ms = 0;

    if (millis() - last_ms < 50) return;
    last_ms = millis();

    // 淡化所有灯 (手动实现 fadeToBlackBy)
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        leds[i].r = leds[i].r > 64 ? leds[i].r - 64 : 0;
        leds[i].g = leds[i].g > 64 ? leds[i].g - 64 : 0;
        leds[i].b = leds[i].b > 64 ? leds[i].b - 64 : 0;
    }

    // 点亮当前位置
    leds[pos] = hsvToRgb(hue, 255, LED_BRIGHTNESS);

    pos = (pos + 1) % NUM_LEDS;
    if (pos == 0) hue += 40;
    FastLED_min<LED_PIN>.show();
}

// ===== 灯效4: 逐个填充 =====
static void effectColorWipe() {
    static uint8_t pos = 0;
    static uint8_t hue = 0;
    static unsigned long last_ms = 0;

    if (millis() - last_ms < 80) return;
    last_ms = millis();

    leds[pos] = hsvToRgb(hue, 255, LED_BRIGHTNESS);
    pos++;

    if (pos >= NUM_LEDS) {
        pos = 0;
        hue += 60;
        // 清空准备下一轮
        for (uint8_t i = 0; i < NUM_LEDS; i++) {
            leds[i] = CRGB::Black;
        }
    }
    FastLED_min<LED_PIN>.show();
}

// ===== 灯效5: 火焰 =====
static void effectFire() {
    static unsigned long last_ms = 0;

    if (millis() - last_ms < 50) return;
    last_ms = millis();

    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        // 随机火焰亮度
        uint8_t flicker = random(100, LED_BRIGHTNESS);
        uint8_t r = flicker;
        uint8_t g = flicker / 3;
        leds[i] = CRGB(r, g, 0);
    }
    FastLED_min<LED_PIN>.show();
}

// ===== 灯效6: 纯色 =====
static void effectSolid() {
    static uint8_t hue = 0;
    static unsigned long last_ms = 0;

    if (millis() - last_ms < 50) return;
    last_ms = millis();

    // 缓慢变色
    CRGB color = hsvToRgb(hue, 255, LED_BRIGHTNESS);
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        leds[i] = color;
    }
    hue++;
    FastLED_min<LED_PIN>.show();
}

// ===== 灯效函数表 =====
typedef void (*EffectFunc)();
static const EffectFunc EFFECT_FUNCS[] = {
    effectRainbow,
    effectBreathing,
    effectComet,
    effectColorWipe,
    effectFire,
    effectSolid,
};

// ===== 初始化 =====
void rgbLedInit() {
    FASTLED_MIN_SETUP(LED_PIN, leds, NUM_LEDS);
    FastLED_min<LED_PIN>.setBrightness(LED_BRIGHTNESS);
    FastLED_min<LED_PIN>.clear();
    FastLED_min<LED_PIN>.show();
}

// ===== 更新当前灯效 =====
void rgbLedUpdate() {
    if (current_effect < EFFECT_COUNT) {
        EFFECT_FUNCS[current_effect]();
    }
}

// ===== 切换下一个灯效 =====
void rgbLedNextEffect() {
    current_effect = (current_effect + 1) % EFFECT_COUNT;
    // 切换时清屏
    FastLED_min<LED_PIN>.clear();
    FastLED_min<LED_PIN>.show();
}

// ===== 设置指定灯效 =====
void rgbLedSetEffect(uint8_t effect) {
    if (effect < EFFECT_COUNT) {
        current_effect = effect;
        FastLED_min<LED_PIN>.clear();
        FastLED_min<LED_PIN>.show();
    }
}

// ===== 获取当前灯效名称 =====
const char *rgbLedGetEffectName() {
    if (current_effect < EFFECT_COUNT) {
        return EFFECT_NAMES[current_effect];
    }
    return "Unknown";
}
