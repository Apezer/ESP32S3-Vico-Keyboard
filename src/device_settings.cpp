#include "device_settings.h"

#include <Preferences.h>
#include <cstring>

namespace {
constexpr uint32_t SETTINGS_MAGIC = 0x56534554;  // 魔数：“VSET”
constexpr uint8_t SETTINGS_VERSION = 1;

struct __attribute__((packed)) StoredSettings {
    uint32_t magic;
    uint8_t version;
    DeviceSettingsData data;
    uint32_t checksum;
};

uint32_t checksum(const uint8_t *bytes, size_t length) {
    uint32_t hash = 2166136261U;
    for (size_t index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= 16777619U;
    }
    return hash;
}

bool valid(const DeviceSettingsData &data) {
    return data.oledBrightness >= 25 && data.oledBrightness <= 100 &&
        data.oledSleepOption <= 3 && data.rgbEffect < 6 &&
        data.rgbBrightness >= 25 && data.rgbBrightness <= 100 &&
        data.rgbSpeed >= 50 && data.rgbSpeed <= 200;
}
}

void DeviceSettings::begin() {
    if (!load()) {
        loadDefaults();
        save();
    }
}

void DeviceSettings::factoryReset() {
    loadDefaults();
    save();
}

bool DeviceSettings::save() const {
    StoredSettings stored = {};
    stored.magic = SETTINGS_MAGIC;
    stored.version = SETTINGS_VERSION;
    stored.data = data_;
    stored.checksum = checksum(
        reinterpret_cast<const uint8_t *>(&stored.version),
        sizeof(stored.version) + sizeof(stored.data)
    );

    Preferences preferences;
    if (!preferences.begin("vico_device", false)) return false;
    const size_t written = preferences.putBytes("settings", &stored, sizeof(stored));
    preferences.end();
    return written == sizeof(stored);
}

uint32_t DeviceSettings::oledSleepMs() const {
    switch (data_.oledSleepOption) {
        case 1: return 60UL * 1000UL;
        case 2: return 5UL * 60UL * 1000UL;
        case 3: return 10UL * 60UL * 1000UL;
        default: return 0;
    }
}

void DeviceSettings::loadDefaults() {
    data_ = {
        75,   // OLED 亮度
        0,    // OLED 始终开启
        true, // 启用 OLED 数字孪生
        0,    // 彩虹灯效
        50,   // RGB 亮度
        100,  // RGB 速度
        true,
    };
}

bool DeviceSettings::load() {
    Preferences preferences;
    if (!preferences.begin("vico_device", true)) return false;

    StoredSettings stored = {};
    const size_t length = preferences.getBytesLength("settings");
    const size_t read = length == sizeof(stored)
        ? preferences.getBytes("settings", &stored, sizeof(stored))
        : 0;
    preferences.end();

    if (read != sizeof(stored) || stored.magic != SETTINGS_MAGIC ||
        stored.version != SETTINGS_VERSION || !valid(stored.data)) {
        return false;
    }
    const uint32_t expected = checksum(
        reinterpret_cast<const uint8_t *>(&stored.version),
        sizeof(stored.version) + sizeof(stored.data)
    );
    if (expected != stored.checksum) return false;

    data_ = stored.data;
    return true;
}
