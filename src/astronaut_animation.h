#pragma once

/**
 * @file astronaut_animation.h
 * @brief 提供内置的 128×64 宇航员 OLED 循环动画。
 *
 * 动画帧以 Adafruit_SSD1306 使用的页优先格式保存在 Flash 中。每帧正好
 * 1024 字节，播放时直接复制到 OLED 帧缓冲区，不需要在主循环中解码 GIF。
 */

#include <Arduino.h>

namespace AstronautAnimation {

constexpr uint16_t WIDTH = 128;
constexpr uint16_t HEIGHT = 64;
constexpr size_t FRAME_BYTES = WIDTH * HEIGHT / 8;
constexpr uint8_t FRAME_COUNT = 20;
constexpr uint16_t FRAME_INTERVAL_MS = 100;

/** @brief 根据动画已经播放的时长计算当前帧编号。 */
uint8_t frameIndex(uint32_t elapsedMs);

/**
 * @brief 返回指定帧在 Flash 中的地址。
 * @param index 帧编号，超出范围时会自动回绕。
 */
const uint8_t *frameData(uint8_t index);

}  // namespace AstronautAnimation
