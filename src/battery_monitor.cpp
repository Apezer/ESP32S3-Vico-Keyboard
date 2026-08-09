/**
 * @file battery_monitor.cpp
 * @brief 实现GPIO14电池ADC采样、低通滤波和锂电池电量曲线。
 */

#include "battery_monitor.h"

#include <cstdlib>

namespace {
// 100K/100K分压的还原倍率。若后续实测存在固定误差，可调整CALIBRATION_PERMILLE。
constexpr uint32_t DIVIDER_MULTIPLIER = 2;
constexpr uint32_t CALIBRATION_PERMILLE = 1000;

struct VoltagePoint {
    uint16_t millivolts;
    uint8_t percent;
};

// 单节锂电池典型静置放电曲线，从低电压到满电排列。
constexpr VoltagePoint DISCHARGE_CURVE[] = {
    {3300, 0}, {3500, 5}, {3600, 10}, {3700, 25}, {3800, 45},
    {3900, 65}, {4000, 80}, {4100, 90}, {4200, 100},
};
}

void BatteryMonitor::begin() {
    pinMode(ADC_PIN, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(ADC_PIN, ADC_11db);

    // C7需要少量时间稳定；连续读取并平均也会丢弃ADC切换后的首批偏差。
    uint32_t total = 0;
    constexpr uint8_t initialSamples = 16;
    for (uint8_t index = 0; index < initialSamples; ++index) {
        total += analogReadMilliVolts(ADC_PIN);
    }
    updateFromAdc(static_cast<uint16_t>(total / initialSamples), true);
    publishedMillivolts_ = state_.millivolts;
    publishedPercent_ = state_.percent;
    publishedValid_ = state_.valid;
    lastSampleAt_ = millis();
}

bool BatteryMonitor::update(uint32_t now) {
    if (now - lastSampleAt_ < SAMPLE_INTERVAL_MS) return false;
    lastSampleAt_ = now;

    updateFromAdc(readAdcMillivolts(), false);
    const bool changed = state_.valid != publishedValid_ ||
        state_.percent != publishedPercent_ ||
        std::abs(static_cast<int>(state_.millivolts) - static_cast<int>(publishedMillivolts_)) >= 10;
    if (changed) {
        publishedMillivolts_ = state_.millivolts;
        publishedPercent_ = state_.percent;
        publishedValid_ = state_.valid;
    }
    return changed;
}

uint16_t BatteryMonitor::readAdcMillivolts() const {
    uint32_t total = 0;
    for (uint8_t index = 0; index < SAMPLES_PER_UPDATE; ++index) {
        total += analogReadMilliVolts(ADC_PIN);
    }
    return static_cast<uint16_t>(total / SAMPLES_PER_UPDATE);
}

void BatteryMonitor::updateFromAdc(uint16_t adcMillivolts, bool initialize) {
    if (initialize || filteredAdcMillivoltsQ8_ == 0) {
        filteredAdcMillivoltsQ8_ = static_cast<uint32_t>(adcMillivolts) << 8;
    } else {
        // EMA alpha=1/8：兼顾电量稳定性和插拔电池后的响应速度。
        const uint32_t sampleQ8 = static_cast<uint32_t>(adcMillivolts) << 8;
        filteredAdcMillivoltsQ8_ = (filteredAdcMillivoltsQ8_ * 7 + sampleQ8) / 8;
    }

    const uint16_t filteredAdc = static_cast<uint16_t>((filteredAdcMillivoltsQ8_ + 128) >> 8);
    state_.valid = filteredAdc >= MIN_VALID_ADC_MV && filteredAdc <= MAX_VALID_ADC_MV;
    if (!state_.valid) {
        state_.millivolts = 0;
        state_.percent = 0;
        return;
    }

    const uint32_t batteryMillivolts =
        static_cast<uint32_t>(filteredAdc) * DIVIDER_MULTIPLIER * CALIBRATION_PERMILLE / 1000;
    state_.millivolts = static_cast<uint16_t>(batteryMillivolts > 5000 ? 5000 : batteryMillivolts);
    state_.percent = voltageToPercent(state_.millivolts);
}

uint8_t BatteryMonitor::voltageToPercent(uint16_t batteryMillivolts) {
    if (batteryMillivolts <= DISCHARGE_CURVE[0].millivolts) return 0;
    constexpr size_t pointCount = sizeof(DISCHARGE_CURVE) / sizeof(DISCHARGE_CURVE[0]);
    if (batteryMillivolts >= DISCHARGE_CURVE[pointCount - 1].millivolts) return 100;

    for (size_t index = 1; index < pointCount; ++index) {
        const auto &upper = DISCHARGE_CURVE[index];
        if (batteryMillivolts > upper.millivolts) continue;
        const auto &lower = DISCHARGE_CURVE[index - 1];
        const uint32_t voltageSpan = upper.millivolts - lower.millivolts;
        const uint32_t percentSpan = upper.percent - lower.percent;
        return static_cast<uint8_t>(lower.percent +
            (batteryMillivolts - lower.millivolts) * percentSpan / voltageSpan);
    }
    return 100;
}
