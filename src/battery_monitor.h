#pragma once

/**
 * @file battery_monitor.h
 * @brief 声明单节锂电池 ADC 采样、滤波和电量估算模块。
 *
 * 硬件使用 100K/100K 分压：VBAT -> 100K -> GPIO14 -> 100K -> GND，
 * GPIO14 对地并联 100nF 电容。因此 ADC 电压约为真实电池电压的一半。
 */

#include <Arduino.h>

/** @brief 电池监测结果；valid=false 表示未检测到合理的电池电压。 */
struct BatterySnapshot {
    uint16_t millivolts = 0;
    uint8_t percent = 0;
    bool valid = false;
};

/**
 * @brief 非阻塞电池监测器。
 *
 * update() 每250ms采样一次并使用指数移动平均抑制ADC噪声。电量百分比采用
 * 单节锂电池静置电压分段曲线估算，比简单线性映射更接近实际放电过程。
 */
class BatteryMonitor {
public:
    static constexpr uint8_t ADC_PIN = 14;

    /** @brief 配置GPIO14 ADC并建立初始滤波值。 */
    void begin();

    /**
     * @brief 到达采样周期时更新电压和百分比。
     * @param now 当前 millis()。
     * @return 对外显示值发生变化时返回true。
     */
    bool update(uint32_t now);

    BatterySnapshot snapshot() const { return state_; }
    uint16_t millivolts() const { return state_.millivolts; }
    uint8_t percent() const { return state_.percent; }
    bool valid() const { return state_.valid; }

private:
    static constexpr uint32_t SAMPLE_INTERVAL_MS = 250;
    static constexpr uint8_t SAMPLES_PER_UPDATE = 4;
    static constexpr uint16_t MIN_VALID_ADC_MV = 500;
    static constexpr uint16_t MAX_VALID_ADC_MV = 2400;

    BatterySnapshot state_ = {};
    uint32_t filteredAdcMillivoltsQ8_ = 0;
    uint32_t lastSampleAt_ = 0;
    uint16_t publishedMillivolts_ = 0;
    uint8_t publishedPercent_ = 0;
    bool publishedValid_ = false;

    uint16_t readAdcMillivolts() const;
    void updateFromAdc(uint16_t adcMillivolts, bool initialize);
    static uint8_t voltageToPercent(uint16_t batteryMillivolts);
};
