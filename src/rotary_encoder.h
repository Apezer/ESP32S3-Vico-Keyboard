#pragma once

/** @file rotary_encoder.h
 *  @brief GPIO11/12/13 EC11 输入。中断只采集相位，主循环消费旋转与按压事件。
 */
#include <Arduino.h>

namespace RotaryEncoder {
constexpr uint8_t PIN_A = 11;
constexpr uint8_t PIN_B = 12;
constexpr uint8_t PIN_SWITCH = 13;
// 若实际安装方向与参考模块相反，只需改为 -1。
constexpr int8_t DIRECTION = 1;

/** @brief 旋钮按压开关经过消抖后的边沿事件。 */
enum class ButtonEvent : uint8_t {
    NONE,
    PRESSED,
    RELEASED,
};

void begin();
/** 原子取出累计整步：正数音量加，负数音量减。 */
int8_t takeSteps();
/** 连续稳定 25ms 后报告按下或松开边沿，长按期间不会重复。 */
ButtonEvent updateButton(uint32_t now);
/** 模式切换/断线时丢弃旧输入，防止重连后补发。 */
void discard();
}
