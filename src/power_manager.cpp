/**
 * @file power_manager.cpp
 * @brief 实现 Vico Keyboard 的分级低功耗休眠和 GPIO 唤醒配置。
 */

#include "power_manager.h"

#include "driver/rtc_io.h"
#include "esp_sleep.h"

void PowerManager::begin(uint32_t now) {
    lastActivityAt_ = now;
}

void PowerManager::noteActivity(uint32_t now) {
    lastActivityAt_ = now;
}

uint32_t PowerManager::idleMilliseconds(uint32_t now) const {
    // 同一轮 loop 中，GATT/USB 回调可能用更新的 millis() 记录活动，而调用方
    // 仍持有稍早的时间快照。此时不能让无符号减法下溢成“已空闲约 49 天”。
    if (static_cast<int32_t>(now - lastActivityAt_) < 0) return 0;
    return now - lastActivityAt_;
}

bool PowerManager::lightSleepDue(uint32_t now) const {
    return idleMilliseconds(now) >= LIGHT_SLEEP_TIMEOUT_MS;
}

bool PowerManager::deepSleepDue(uint32_t now) const {
    return idleMilliseconds(now) >= DEEP_SLEEP_TIMEOUT_MS;
}

[[noreturn]] void PowerManager::enterDeepSleep(uint64_t wakePinMask) {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    // 当前 Arduino Core 使用 EXT1 低电平唤醒。保持 RTC 外设供电，并显式开启
    // 上拉，防止没有外部上拉电阻的按键输入在深睡期间漂移或立即误唤醒。
    for (uint8_t pin = 0; pin <= 21; ++pin) {
        if ((wakePinMask & (1ULL << pin)) == 0) continue;
        const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
        rtc_gpio_pullup_en(gpio);
        rtc_gpio_pulldown_dis(gpio);
    }
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ext1_wakeup(wakePinMask, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

bool PowerManager::wokeFromDeepSleep() {
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    return cause == ESP_SLEEP_WAKEUP_GPIO || cause == ESP_SLEEP_WAKEUP_EXT1;
}
