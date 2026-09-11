/** @file rotary_encoder.cpp
 *  @brief EC11 双相 CHANGE 中断与独立按压消抖。
 */
#include "rotary_encoder.h"
#include "encoder_decoder.h"

namespace {
portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
EncoderDecoder decoder;
volatile int8_t pendingSteps = 0;
bool initialized = false;
bool buttonRaw = false;
bool buttonStable = false;
uint32_t buttonChangedAt = 0;

uint8_t readPhase() {
    return (digitalRead(RotaryEncoder::PIN_A) << 1) | digitalRead(RotaryEncoder::PIN_B);
}

// 使用普通 GPIO 中断（不声明 IRAM 安全）；ISR 不调用 HID、显示或 Flash 操作。
void onPhaseChange() {
    portENTER_CRITICAL_ISR(&encoderMux);
    const int8_t step = decoder.update(readPhase()) * RotaryEncoder::DIRECTION;
    const int next = pendingSteps + step;
    pendingSteps = next > 32 ? 32 : next < -32 ? -32 : next;
    portEXIT_CRITICAL_ISR(&encoderMux);
}
}

void RotaryEncoder::begin() {
    pinMode(PIN_A, INPUT_PULLUP);
    pinMode(PIN_B, INPUT_PULLUP);
    pinMode(PIN_SWITCH, INPUT_PULLUP);
    initialized = true;
    discard();
    attachInterrupt(digitalPinToInterrupt(PIN_A), onPhaseChange, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_B), onPhaseChange, CHANGE);
}

int8_t RotaryEncoder::takeSteps() {
    portENTER_CRITICAL(&encoderMux);
    const int8_t steps = pendingSteps;
    pendingSteps = 0;
    portEXIT_CRITICAL(&encoderMux);
    return steps;
}

bool RotaryEncoder::updateButton(uint32_t now) {
    const bool pressed = digitalRead(PIN_SWITCH) == LOW;
    if (pressed != buttonRaw) {
        buttonRaw = pressed;
        buttonChangedAt = now;
    }
    if (buttonStable == buttonRaw || now - buttonChangedAt < 25) return false;
    buttonStable = buttonRaw;
    return buttonStable;
}

void RotaryEncoder::discard() {
    if (!initialized) return;
    portENTER_CRITICAL(&encoderMux);
    pendingSteps = 0;
    decoder.reset(readPhase());
    portEXIT_CRITICAL(&encoderMux);
    buttonRaw = buttonStable = digitalRead(PIN_SWITCH) == LOW;
    buttonChangedAt = millis();
}
