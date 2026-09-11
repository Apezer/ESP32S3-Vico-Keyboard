#pragma once

/**
 * @file encoder_decoder.h
 * @brief EC11 正交解码：过滤触点往返抖动，在完整四相周期结束时输出一步。
 */
#include <stdint.h>

class EncoderDecoder {
public:
    void reset(uint8_t state) { previous_ = state & 3; quarterSteps_ = 0; }

    /** @return 与参考驱动同向：+1 为顺时针，-1 为逆时针，0 为未完成。 */
    int8_t update(uint8_t state) {
        state &= 3;
        const uint8_t transition = (previous_ << 2) | state;
        // 两相同时变化说明丢失了边沿，放弃当前半步，禁止猜测方向。
        if ((previous_ ^ state) == 3) quarterSteps_ = 0;
        previous_ = state;
        switch (transition) {
            case 0x1: case 0x7: case 0xE: case 0x8: ++quarterSteps_; break;
            case 0x2: case 0x4: case 0xD: case 0xB: --quarterSteps_; break;
            default: break;
        }
        // 上拉电路的两相均断开（11）是整步落点；反向抖动会抵消累积值。
        if (state != 3) return 0;
        const int8_t step = quarterSteps_ >= 4 ? 1 : quarterSteps_ <= -4 ? -1 : 0;
        quarterSteps_ = 0;
        return step;
    }

private:
    uint8_t previous_ = 3;
    int8_t quarterSteps_ = 0;
};
