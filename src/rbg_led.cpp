/**
 * rbg_led.cpp - WS2812B RGB 灯带效果。
 */

#include "rbg_led.h"

#include <FastLED_min.h>

static CRGB leds[RBG_LED_COUNT];
static uint8_t current_effect = RBG_EFFECT_RAINBOW;
static uint8_t brightness_percent = 50;
static uint8_t speed_percent = 100;
static bool leds_enabled = true;

static const char *const EFFECT_NAMES[] = {
    "Rainbow",
    "Breathing",
    "Comet",
    "Wipe",
    "Fire",
    "Solid",
};

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
    static uint8_t value = 0;
    static int8_t direction = 1;
    static unsigned long last_ms = 0;

    if (!elapsed(last_ms, scaledInterval(18))) {
        return;
    }

    value = value + direction * 2;
    const uint8_t peak = peakBrightness();
    if (value >= peak) {
        value = peak;
        direction = -1;
    } else if (value <= 2) {
        value = 2;
        direction = 1;
    }

    const CRGB color(value, 0, value / 2);
    for (uint8_t i = 0; i < RBG_LED_COUNT; i++) {
        leds[i] = color;
    }
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

typedef void (*EffectFunc)();
static const EffectFunc EFFECT_FUNCS[] = {
    effectRainbow,
    effectBreathing,
    effectComet,
    effectColorWipe,
    effectFire,
    effectSolidFade,
};

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
    if (effect < RBG_EFFECT_COUNT) {
        current_effect = effect;
        clearLeds();
    }
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
    brightness_percent = constrain(percent, 25, 100);
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

void rbgLedSetEnabled(bool enabled) {
    leds_enabled = enabled;
    clearLeds();
}

bool rbgLedIsEnabled() {
    return leds_enabled;
}
