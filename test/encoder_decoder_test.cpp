/** @file encoder_decoder_test.cpp
 *  @brief 主机端验证正反转、触点抖动、半步回退和漏边沿后的恢复。
 */
#include "../src/encoder_decoder.h"
#include <assert.h>
#include <initializer_list>

int run(EncoderDecoder &decoder, std::initializer_list<uint8_t> phases) {
    int result = 0;
    for (uint8_t phase : phases) result += decoder.update(phase);
    return result;
}

int main() {
    EncoderDecoder decoder;
    decoder.reset(3);
    assert(run(decoder, {2, 0, 1, 3}) == 1);
    assert(run(decoder, {1, 0, 2, 3}) == -1);
    // 每条边沿均夹杂机械往返抖动，仍只能得到一个完整动作。
    assert(run(decoder, {2, 3, 2, 0, 2, 0, 1, 0, 1, 3}) == 1);
    assert(run(decoder, {1, 3, 1, 0, 1, 0, 2, 0, 2, 3}) == -1);
    assert(run(decoder, {2, 0, 2, 3}) == 0);
    assert(run(decoder, {1, 0, 1, 3}) == 0);
    assert(run(decoder, {0, 1, 3}) == 0);
    assert(run(decoder, {2, 0, 1, 3}) == 1);
    assert(run(decoder, {3, 3, 3}) == 0);
    decoder.reset(0);
    assert(run(decoder, {1, 3}) == 0);
    assert(run(decoder, {2, 0, 1, 3, 2, 0, 1, 3}) == 2);
    return 0;
}
