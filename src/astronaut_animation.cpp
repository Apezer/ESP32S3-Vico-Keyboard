/**
 * @file astronaut_animation.cpp
 * @brief 实现内置宇航员动画的帧索引与 Flash 数据访问。
 */

#include "astronaut_animation.h"

#include <pgmspace.h>

namespace AstronautAnimation {
namespace {

const uint8_t FRAMES[FRAME_COUNT * FRAME_BYTES] PROGMEM = {
#include "astronaut_animation_frames.inc"
};

static_assert(sizeof(FRAMES) == FRAME_COUNT * FRAME_BYTES,
              "Astronaut animation frame data has an unexpected size.");

}  // namespace

uint8_t frameIndex(uint32_t elapsedMs) {
    return static_cast<uint8_t>((elapsedMs / FRAME_INTERVAL_MS) % FRAME_COUNT);
}

const uint8_t *frameData(uint8_t index) {
    return FRAMES + (index % FRAME_COUNT) * FRAME_BYTES;
}

}  // namespace AstronautAnimation
