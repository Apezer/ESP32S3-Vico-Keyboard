/**
 * rbg_led.h - WS2812B RGB strip effects.
 *
 * Hardware: 8 WS2812B LEDs, DIN on GPIO 17.
 */

#ifndef RBG_LED_H
#define RBG_LED_H

#include <Arduino.h>

#define RBG_LED_PIN         17
#define RBG_LED_COUNT       8
#define RBG_LED_BRIGHTNESS  40

enum RbgLedEffect : uint8_t {
    RBG_EFFECT_RAINBOW = 0,
    RBG_EFFECT_BREATHING,
    RBG_EFFECT_COMET,
    RBG_EFFECT_COLOR_WIPE,
    RBG_EFFECT_FIRE,
    RBG_EFFECT_SOLID_FADE,
    RBG_EFFECT_COUNT,
};

void rbgLedInit();
void rbgLedUpdate();
void rbgLedNextEffect();
void rbgLedSetEffect(uint8_t effect);
uint8_t rbgLedGetEffect();
const char *rbgLedGetEffectName();

#endif /* RBG_LED_H */
