#pragma once

/**
 * @file power_manager.h
 * @brief BLE 电池模式的空闲计时和 ESP32-S3 深度睡眠入口。
 *
 * USB 模式不使用本模块进入休眠。15～30 秒的连接空闲态由 main.cpp 管理，
 * 本模块负责累计空闲时间，以及最终由按键和旋钮按压唤醒的深度睡眠。
 */

#include <Arduino.h>

class PowerManager {
public:
    // 临时测试参数：15 秒连接空闲，累计 30 秒深睡。测试完成后恢复为 5/15 分钟。
    static constexpr uint32_t LIGHT_SLEEP_TIMEOUT_MS = 15UL * 1000UL;
    static constexpr uint32_t DEEP_SLEEP_TIMEOUT_MS = 30UL * 1000UL;

    void begin(uint32_t now);
    void noteActivity(uint32_t now);
    uint32_t idleMilliseconds(uint32_t now) const;
    bool lightSleepDue(uint32_t now) const;
    bool deepSleepDue(uint32_t now) const;

    [[noreturn]] void enterDeepSleep(uint64_t wakePinMask);
    static bool wokeFromDeepSleep();

private:
    uint32_t lastActivityAt_ = 0;
};
