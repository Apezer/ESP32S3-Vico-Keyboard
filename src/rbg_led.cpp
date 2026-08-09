/**
 * @file rbg_led.cpp
 * @brief 实现八种非阻塞 WS2812B 灯效及颜色、亮度和速度参数。
 */

#include "rbg_led.h"

#include <FastLED_min.h>

// =============================================================================
// 模块状态与灯效名称
// =============================================================================
// 所有效果函数只修改 leds[] 并根据自己的时间戳决定是否刷新，不使用 delay()，
// 因此不会主动阻塞键盘扫描循环。
static CRGB leds[RBG_LED_COUNT];
static uint8_t current_effect = RBG_EFFECT_RAINBOW;
static uint8_t brightness_percent = 50;
static uint8_t speed_percent = 100;
static bool leds_enabled = true;
static CRGB solid_color(214, 255, 56);
static bool solid_color_dirty = true;

static const char *const EFFECT_NAMES[] = {
    "Rainbow",
    "Breathing",
    "Comet",
    "Wipe",
    "Fire",
    "Solid",
    "Static",
    "Rainbow Breath",
};

// =============================================================================
// 颜色、时间和输出辅助函数
// =============================================================================

static CRGB hsvToRgb(uint8_t h, uint8_t s, uint8_t v) {
    const uint8_t region = h / 43;
    const uint8_t remainder = (h - (region * 43)) * 6;
    const uint8_t p = (v * (255 - s)) / 255;
    const uint8_t q = (v * (255 - ((s * remainder) / 255))) / 255;
    const uint8_t t = (v * (255 - ((s * (255 - remainder)) / 255))) / 255;

    switch (region) {
        case 0:  return CRGB(v, t, p);
        case 1:  return CRGB(q, v, p);
        case 2:  return CRGB(p, v, t);
        case 3:  return CRGB(p, q, v);
        case 4:  return CRGB(t, p, v);
        default: return CRGB(v, p, q);
    }
}

static void showLeds() {
    FastLED_min<RBG_LED_PIN>.show();
}

static void clearLeds() {
    FastLED_min<RBG_LED_PIN>.clear();
    showLeds();
    solid_color_dirty = true;
}

static bool elapsed(unsigned long &last_ms, unsigned long interval_ms) {
    const unsigned long now = millis();
    if (now - last_ms < interval_ms) {
        return false;
    }
    last_ms = now;
    return true;
}

static unsigned long scaledInterval(unsigned long base_ms) {
    return max(5UL, base_ms * 100UL / speed_percent);
}

static uint8_t peakBrightness() {
    return static_cast<uint8_t>(brightness_percent * 255UL / 100UL);
}

struct BreathAnimation {
    uint8_t level = 0;
    int8_t direction = 1;
    unsigned long last_ms = 0;
};

/** 按当前呼吸亮度和全局亮度缩放一种 RGB 颜色。 */
static CRGB breathingColor(const CRGB &color, uint8_t level) {
    const uint8_t output_level = static_cast<uint8_t>(level * peakBrightness() / 255UL);
    return CRGB(
        static_cast<uint8_t>(color.r * output_level / 255UL),
        static_cast<uint8_t>(color.g * output_level / 255UL),
        static_cast<uint8_t>(color.b * output_level / 255UL)
    );
}

// =============================================================================
// 内置灯效实现
// =============================================================================

static void effectRainbow() {
    static uint8_t hue = 0;
    static unsigned long last_ms = 0;

    if (!elapsed(last_ms, scaledInterval(30))) {
        return;
    }

    for (uint8_t i = 0; i < RBG_LED_COUNT; i++) {
        leds[i] = hsvToRgb(hue + i * 32, 255, peakBrightness());
    }
    hue += 3;
    showLeds();
}

static void effectBreathing() {
    static BreathAnimation animation;
    if (!elapsed(animation.last_ms, scaledInterval(18))) return;

    const int16_t next = static_cast<int16_t>(animation.level) + animation.direction * 2;
    if (next >= 255) {
        animation.level = 255;
        animation.direction = -1;
    } else if (next <= 0) {
        animation.level = 0;
        animation.direction = 1;
    } else {
        animation.level = static_cast<uint8_t>(next);
    }

    const CRGB color = breathingColor(solid_color, animation.level);
    for (uint8_t i = 0; i < RBG_LED_COUNT; i++) {
        leds[i] = color;
    }
    showLeds();
}

/** 每完成一次完整呼吸后跳到下一种彩虹颜色。 */
static void effectRainbowBreathing() {
    static BreathAnimation animation;
    static uint8_t hue = 0;
    if (!elapsed(animation.last_ms, scaledInterval(18))) return;

    const int16_t next = static_cast<int16_t>(animation.level) + animation.direction * 2;
    if (next >= 255) {
        animation.level = 255;
        animation.direction = -1;
    } else if (next <= 0) {
        animation.level = 0;
        animation.direction = 1;
        hue += 43;
    } else {
        animation.level = static_cast<uint8_t>(next);
    }

    const uint8_t output_level = static_cast<uint8_t>(animation.level * peakBrightness() / 255UL);
    const CRGB color = hsvToRgb(hue, 255, output_level);
    for (uint8_t i = 0; i < RBG_LED_COUNT; i++) leds[i] = color;
    showLeds();
}

static void effectComet() {
    static uint8_t pos = 0;
    static uint8_t hue = 0;
    static unsigned long last_ms = 0;

    if (!elapsed(last_ms, scaledInterval(55))) {
        return;
    }

    for (uint8_t i = 0; i < RBG_LED_COUNT; i++) {
        leds[i].r = leds[i].r > 48 ? leds[i].r - 48 : 0;
        leds[i].g = leds[i].g > 48 ? leds[i].g - 48 : 0;
        leds[i].b = leds[i].b > 48 ? leds[i].b - 48 : 0;
    }

    leds[pos] = hsvToRgb(hue, 255, peakBrightness());
    pos = (pos + 1) % RBG_LED_COUNT;
    if (pos == 0) {
        hue += 40;
    }
    showLeds();
}

static void effectColorWipe() {
    static uint8_t pos = 0;
    static uint8_t hue = 0;
    static unsigned long last_ms = 0;

    if (!elapsed(last_ms, scaledInterval(85))) {
        return;
    }

    leds[pos] = hsvToRgb(hue, 255, peakBrightness());
    pos++;
    if (pos >= RBG_LED_COUNT) {
        pos = 0;
        hue += 50;
        for (uint8_t i = 0; i < RBG_LED_COUNT; i++) {
            leds[i] = CRGB::Black;
        }
    }
    showLeds();
}

static void effectFire() {
    static unsigned long last_ms = 0;

    if (!elapsed(last_ms, scaledInterval(45))) {
        return;
    }

    for (uint8_t i = 0; i < RBG_LED_COUNT; i++) {
        const uint8_t flicker = random(12, peakBrightness() + 1);
        leds[i] = CRGB(flicker, flicker / 3, 0);
    }
    showLeds();
}

static void effectSolidFade() {
    static uint8_t hue = 0;
    static unsigned long last_ms = 0;

    if (!elapsed(last_ms, scaledInterval(50))) {
        return;
    }

    const CRGB color = hsvToRgb(hue, 255, peakBrightness());
    for (uint8_t i = 0; i < RBG_LED_COUNT; i++) {
        leds[i] = color;
    }
    hue++;
    showLeds();
}

/** 保持全部按键为用户选择的同一种颜色，不执行动画。 */
static void effectSolidColor() {
    static CRGB last_color = CRGB::Black;
    static uint8_t last_brightness = 0;
    const uint8_t peak = peakBrightness();
    if (!solid_color_dirty && last_color.r == solid_color.r && last_color.g == solid_color.g &&
        last_color.b == solid_color.b && last_brightness == peak) {
        return;
    }

    const CRGB output(
        static_cast<uint8_t>(solid_color.r * peak / 255UL),
        static_cast<uint8_t>(solid_color.g * peak / 255UL),
        static_cast<uint8_t>(solid_color.b * peak / 255UL)
    );
    for (uint8_t i = 0; i < RBG_LED_COUNT; i++) leds[i] = output;
    last_color = solid_color;
    last_brightness = peak;
    solid_color_dirty = false;
    showLeds();
}

typedef void (*EffectFunc)();
static const EffectFunc EFFECT_FUNCS[] = {
    effectRainbow,
    effectBreathing,
    effectComet,
    effectColorWipe,
    effectFire,
    effectSolidFade,
    effectSolidColor,
    effectRainbowBreathing,
};

// =============================================================================
// 公共灯带控制接口
// =============================================================================

void rbgLedInit() {
    FASTLED_MIN_SETUP(RBG_LED_PIN, leds, RBG_LED_COUNT);
    FastLED_min<RBG_LED_PIN>.setBrightness(255);
    clearLeds();
}

void rbgLedUpdate() {
    if (leds_enabled && current_effect < RBG_EFFECT_COUNT) {
        EFFECT_FUNCS[current_effect]();
    }
}

void rbgLedNextEffect() {
    current_effect = (current_effect + 1) % RBG_EFFECT_COUNT;
    clearLeds();
}

void rbgLedSetEffect(uint8_t effect) {
    if (effect >= RBG_EFFECT_COUNT || effect == current_effect) return;

    current_effect = effect;
    clearLeds();
}

uint8_t rbgLedGetEffect() {
    return current_effect;
}

const char *rbgLedGetEffectName() {
    if (current_effect < RBG_EFFECT_COUNT) {
        return EFFECT_NAMES[current_effect];
    }
    return "Unknown";
}

void rbgLedSetBrightness(uint8_t percent) {
    const uint8_t next_brightness = constrain(percent, 25, 100);
    if (next_brightness == brightness_percent) return;

    brightness_percent = next_brightness;
    solid_color_dirty = true;
}

uint8_t rbgLedGetBrightness() {
    return brightness_percent;
}

void rbgLedSetSpeed(uint8_t percent) {
    speed_percent = constrain(percent, 50, 200);
}

uint8_t rbgLedGetSpeed() {
    return speed_percent;
}

void rbgLedSetColor(uint8_t red, uint8_t green, uint8_t blue) {
    if (solid_color.r == red && solid_color.g == green && solid_color.b == blue) return;

    solid_color = CRGB(red, green, blue);
    // 颜色选择器会连续下发更新。这里只标记下一帧需要刷新，避免每个数据包
    // 都先把灯带清成黑色，从而造成常亮模式切换颜色后看起来一直不亮。
    solid_color_dirty = true;
}

void rbgLedSetEnabled(bool enabled) {
    if (leds_enabled == enabled) return;

    leds_enabled = enabled;
    if (enabled) {
        // 重新打开时由当前灯效绘制完整画面。
        solid_color_dirty = true;
    } else {
        clearLeds();
    }
}

bool rbgLedIsEnabled() {
    return leds_enabled;
}
