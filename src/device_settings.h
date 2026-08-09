#pragma once

/**
 * @file device_settings.h
 * @brief 声明独立于按键预设的设备级持久化设置。
 *
 * 本模块保存 OLED、数字孪生和 RGB 灯带参数。数据最终写入 ESP32 NVS，
 * 与五套按键预设分开管理，恢复按键预设时不会意外改变设备显示设置。
 */

#include <Arduino.h>

/** @brief 写入 NVS 的紧凑设备设置。修改字段时必须同步提升存储版本。 */
struct __attribute__((packed)) DeviceSettingsData {
    uint8_t oledBrightness;
    uint8_t oledSleepOption;
    bool oledTwinEnabled;
    uint8_t oledPage;
    bool oledAutoClaude;
    uint8_t rgbEffect;
    uint8_t rgbBrightness;
    uint8_t rgbSpeed;
    bool rgbEnabled;
    uint8_t rgbRed;
    uint8_t rgbGreen;
    uint8_t rgbBlue;
};

/** 独立于五套按键预设的持久化设备设置。 */
class DeviceSettings {
public:
    /** @brief 初始化设置；存储无效时恢复默认值并立即保存。 */
    void begin();
    /** @brief 恢复并保存全部设备级出厂设置。 */
    void factoryReset();
    /** @brief 校验当前数据并保存到 NVS。 */
    bool save() const;

    const DeviceSettingsData &data() const { return data_; }
    DeviceSettingsData &edit() { return data_; }
    /** @brief 将睡眠选项转换为毫秒；返回 0 表示常亮。 */
    uint32_t oledSleepMs() const;

private:
    DeviceSettingsData data_ = {};

    void loadDefaults();
    bool load();
};
