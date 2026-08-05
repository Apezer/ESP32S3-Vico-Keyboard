#pragma once

#include <Arduino.h>

struct __attribute__((packed)) DeviceSettingsData {
    uint8_t oledBrightness;
    uint8_t oledSleepOption;
    bool oledTwinEnabled;
    uint8_t rgbEffect;
    uint8_t rgbBrightness;
    uint8_t rgbSpeed;
    bool rgbEnabled;
};

/** 独立于五套按键预设的持久化设备设置。 */
class DeviceSettings {
public:
    void begin();
    void factoryReset();
    bool save() const;

    const DeviceSettingsData &data() const { return data_; }
    DeviceSettingsData &edit() { return data_; }
    uint32_t oledSleepMs() const;

private:
    DeviceSettingsData data_ = {};

    void loadDefaults();
    bool load();
};
