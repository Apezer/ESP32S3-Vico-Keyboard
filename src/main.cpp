/**
 * @file main.cpp
 * @brief ESP32-S3 USB/蓝牙双模式 Vico Keyboard 主程序。
 *
 * 硬件：DFRobot FireBeetle2 ESP32-S3 + 0.96 英寸 OLED（SSD1306，I2C）
 *
 * 主循环职责：模式热切换、8 键消抖与 HID 报告、设备设置菜单、OLED 页面、
 * Claude Code 状态展示、RGB 灯效以及 USB OLED 数字孪生传输。
 *
 * 接线：
 *   OLED SDA -> GPIO 10
 *   OLED SCL -> GPIO 9
 *   OLED VCC -> 3.3V
 *   OLED GND -> GND
 *
 *   K1 -> GPIO 18  （左方向键）
 *   K2 -> GPIO 17  （下方向键）
 *   K3 -> GPIO 16  （右方向键）
 *   K4 -> GPIO 15  （回车键）
 *   K5 -> GPIO 5   （退格键）
 *   K6 -> GPIO 6   （上方向键）
 *   K7 -> GPIO 7   （Ctrl + Win）
 *   K8 -> GPIO 4   （Fn，内部层切换键）
 *
 *   WS2812B DIN -> GPIO 8
 *   模式开关公共端 -> GPIO 38
 *     LOW/GND  = 蓝牙模式
 *     HIGH/3V3 = USB 模式
 *
 * 依赖库（已在 platformio.ini 中配置）：
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *   - NimBLE-Arduino
 *   - ESP32-NimBLE-Keyboard
 *   - ESP32 Arduino USB + USBHIDKeyboard
 *   - FastLED_min
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NimBleKeyboard.h>
#include "USB.h"
#include "USBHIDConsumerControl.h"
#include "USBHIDKeyboard.h"
#include "tusb.h"
#include "battery_monitor.h"
#include "font.h"
#include "device_settings.h"
#include "key_profiles.h"
#include "oled_runtime.h"
#include "oled_twin.h"
#include "rbg_led.h"

#if !ARDUINO_USB_MODE
#error "Native USB must be enabled with ARDUINO_USB_MODE=1."
#endif

// =============================================================================
// 硬件引脚与基础时序
// =============================================================================
// KEY_PINS 按逻辑 KEY1～KEY8 排列；此顺序同时用于预设、OLED 标签和 HID 报告。
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDR       0x3C

#define I2C_SDA         10
#define I2C_SCL         9
#define MODE_SELECT_PIN 38

#define NUM_KEYS        8
#define DEBOUNCE_MS     10
#define MODE_DEBOUNCE_MS 50
#define USB_PROMPT_FRAME_MS 250

const uint8_t KEY_PINS[VICO_KEY_COUNT] = {18, 17, 16, 15, 5, 6, 7, 4};

/** @brief 当前实际启用的 HID 传输模式，由 GPIO38 拨片开关热切换。 */
enum class KeyboardMode : uint8_t {
    BLE,
    USB,
};

// =============================================================================
// 按键扫描、连接和 OLED 电源运行状态
// =============================================================================
// key_state 保存消抖后的物理状态；last_reported 保存上一轮已处理状态。
// 两者分离后，主循环可以只在边沿变化时发送 HID 事件。
static bool key_state[NUM_KEYS] = {false};
static bool last_reported[NUM_KEYS] = {false};
static unsigned long last_change[NUM_KEYS] = {0};
static bool fn_pressed = false;
static int8_t fn_source_key = -1;
static bool profile_switch_consumed[NUM_KEYS] = {false};
static unsigned long profile_notice_until = 0;
static bool display_dirty = true;
static KeyboardMode keyboard_mode = KeyboardMode::BLE;
static bool mode_switch_raw_usb = false;
static unsigned long mode_switch_changed_at = 0;
static bool usb_started = false;
static bool usb_mounted = false;
static uint8_t usb_prompt_frame = 0;
static unsigned long usb_prompt_frame_at = 0;
static unsigned long last_user_activity_at = 0;
static bool oled_sleeping = false;

/** @brief 设备端设置菜单的页面状态。 */
enum class SettingsScreen : uint8_t {
    MAIN,
    PROFILE,
    OLED,
    RGB,
    CONNECTION,
    KEY_TEST,
    DEVICE_INFO,
    FACTORY_RESET,
};

/** @brief 设置菜单导航状态；waitForRelease 防止入菜单组合键被重复消费。 */
struct SettingsMenuState {
    bool active = false;
    bool waitForRelease = false;
    SettingsScreen screen = SettingsScreen::MAIN;
    uint8_t selection = 0;
};

static SettingsMenuState settingsMenu;

// =============================================================================
// 硬件驱动与功能模块对象
// =============================================================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
OledRuntime oledRuntime;
BatteryMonitor batteryMonitor;

/** @brief 在标准 BLE HID 服务启动阶段追加 Vico OLED 状态 GATT 服务。 */
class VicoBleKeyboard : public BleKeyboard {
public:
    VicoBleKeyboard() : BleKeyboard("Vico Keyboard", "Apezer", 100) {}

    uint8_t connectionCount() {
        return isConnected() ? 1 : 0;
    }

protected:
    void onStarted(NimBLEServer *server) override {
        oledRuntime.beginBle(server);
    }
};

VicoBleKeyboard bleKeyboard;
USBHIDKeyboard usbKeyboard;
USBHIDConsumerControl usbConsumer;
KeyProfileManager keyProfiles;
DeviceSettings deviceSettings;
OledTwinTransport oledTwin;

void applyOledSettings();
void applyRgbSettings();
void enterSettingsMenu();
void exitSettingsMenu();
void handleSettingsKeyPress(uint8_t index);
void renderSettingsMenu();

// =============================================================================
// OLED 帧提交、唤醒和自动休眠
// =============================================================================

/** 将帧缓冲区推送到物理 OLED，然后截取该精确画面。 */
void commitOledFrame() {
    display.display();
    if (deviceSettings.data().oledTwinEnabled) {
        oledTwin.captureFrame(display.getBuffer(), OledTwinTransport::FRAME_BYTES);
    }
}

void wakeOled() {
    last_user_activity_at = millis();
    if (!oled_sleeping) return;
    display.ssd1306_command(SSD1306_DISPLAYON);
    oled_sleeping = false;
    display_dirty = true;
}

void updateOledSleep(unsigned long now) {
    if (settingsMenu.active || oled_sleeping) return;
    const uint32_t timeout = deviceSettings.oledSleepMs();
    if (timeout != 0 && now - last_user_activity_at >= timeout) {
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        oled_sleeping = true;
    }
}

void resetKeyTracking() {
    const unsigned long now = millis();
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        key_state[i] = false;
        last_reported[i] = false;
        last_change[i] = now;
    }
    fn_pressed = false;
    fn_source_key = -1;
    memset(profile_switch_consumed, 0, sizeof(profile_switch_consumed));
}

// =============================================================================
// USB/BLE 接口生命周期与拨片热切换
// =============================================================================
// 切换模式时先释放旧 HID 状态并关闭旧接口，再启动目标接口，避免修饰键残留
// 或 USB/BLE 协议栈同时持有同一组按键状态。

void startKeyboardInterface(KeyboardMode mode) {
    if (mode == KeyboardMode::USB) {
        usb_mounted = false;
        if (!usb_started) {
            // 必须在原生 USB 启动前注册 HID 接口。
            USB.manufacturerName("Apezer");
            USB.productName("Vico Keyboard");
            usbKeyboard.begin();
            usbConsumer.begin();
            oledTwin.begin(&keyProfiles, &oledRuntime);
            usb_started = USB.begin();
        } else {
            tud_connect();
        }
    } else {
        // setBatteryLevel()在BLE服务创建前只更新缓存初值，服务启动时会自动发布。
        bleKeyboard.setBatteryLevel(batteryMonitor.valid() ? batteryMonitor.percent() : 0);
        bleKeyboard.begin();
    }
}

void stopKeyboardInterface(KeyboardMode mode) {
    if (mode == KeyboardMode::USB) {
        if (usb_started) {
            usbKeyboard.releaseAll();
            usbConsumer.release();
            delay(5);
            oledTwin.resetSession();
            tud_disconnect();
            usb_mounted = false;
        }
    } else {
        bleKeyboard.end();
        oledRuntime.endBle();
    }
}

void switchKeyboardMode(KeyboardMode next_mode) {
    if (next_mode == keyboard_mode) {
        return;
    }

    stopKeyboardInterface(keyboard_mode);
    delay(20);

    keyboard_mode = next_mode;
    resetKeyTracking();
    startKeyboardInterface(keyboard_mode);

    Serial.printf(
        "Keyboard mode switched to %s\n",
        keyboard_mode == KeyboardMode::USB ? "USB" : "BLE"
    );
    display_dirty = true;
}

void updateModeSwitch() {
    const bool raw_usb = digitalRead(MODE_SELECT_PIN) == HIGH;
    const unsigned long now = millis();

    if (raw_usb != mode_switch_raw_usb) {
        mode_switch_raw_usb = raw_usb;
        mode_switch_changed_at = now;
        return;
    }

    const KeyboardMode requested_mode = raw_usb
        ? KeyboardMode::USB
        : KeyboardMode::BLE;

    if (requested_mode != keyboard_mode &&
        now - mode_switch_changed_at >= MODE_DEBOUNCE_MS) {
        switchKeyboardMode(requested_mode);
    }
}

void updateUsbConnectionState(unsigned long now) {
    if (keyboard_mode != KeyboardMode::USB || !usb_started) {
        return;
    }

    const bool mounted_now = tud_mounted();
    if (mounted_now != usb_mounted) {
        usb_mounted = mounted_now;
        resetKeyTracking();
        display_dirty = true;
        Serial.printf(
            "USB host %s\n",
            usb_mounted ? "connected" : "disconnected"
        );
    }

    if (!usb_mounted && now - usb_prompt_frame_at >= USB_PROMPT_FRAME_MS) {
        usb_prompt_frame_at = now;
        usb_prompt_frame = (usb_prompt_frame + 1) % 4;
        display_dirty = true;
    }
}

// =============================================================================
// HID 按键、修饰键和媒体键事件
// =============================================================================
// 这一层屏蔽 USB 与 BLE API 差异，上层只处理 KeyBinding 和物理按键边沿。

void pressHidKey(uint8_t keycode) {
    if (keyboard_mode == KeyboardMode::USB && usb_mounted) {
        // USBHIDKeyboard::press() 会为 Arduino 风格键码 0x80～0x87
        // 更新修饰键位图，但随后调用 pressRaw(0)。本项目使用的
        // ESP32 Arduino Core 中，pressRaw(0) 会直接退出且不发送报告，
        // 因此 Ctrl+GUI 这类仅含修饰键的绑定无法到达主机。
        //
        // 原始 HID 修饰键 Usage 为 0xE0～0xE7。通过 pressRaw() 发送时，
        // 会更新位图并立即传输报告；非修饰键仍使用库的常规转换逻辑。
        constexpr uint8_t ARDUINO_MODIFIER_FIRST = 0x80;
        constexpr uint8_t ARDUINO_MODIFIER_LAST = 0x87;
        constexpr uint8_t HID_MODIFIER_FIRST = 0xE0;

        if (keycode >= ARDUINO_MODIFIER_FIRST && keycode <= ARDUINO_MODIFIER_LAST) {
            const uint8_t raw_modifier =
                HID_MODIFIER_FIRST + (keycode - ARDUINO_MODIFIER_FIRST);
            usbKeyboard.pressRaw(raw_modifier);
        } else {
            usbKeyboard.press(keycode);
        }
    } else if (bleKeyboard.isConnected()) {
        bleKeyboard.press(keycode);
    }
}

void releaseAllHid() {
    if (keyboard_mode == KeyboardMode::USB && usb_mounted) {
        usbKeyboard.releaseAll();
        usbConsumer.release();
    } else if (bleKeyboard.isConnected()) {
        bleKeyboard.releaseAll();
    }
}

void pressModifiers(uint8_t modifiers) {
    if (modifiers & MOD_CTRL) pressHidKey(KEY_LEFT_CTRL);
    if (modifiers & MOD_SHIFT) pressHidKey(KEY_LEFT_SHIFT);
    if (modifiers & MOD_ALT) pressHidKey(KEY_LEFT_ALT);
    if (modifiers & MOD_GUI) pressHidKey(KEY_LEFT_GUI);
}

uint16_t usbConsumerUsage(ConsumerAction action) {
    switch (action) {
        case ConsumerAction::PREVIOUS_TRACK: return CONSUMER_CONTROL_SCAN_PREVIOUS;
        case ConsumerAction::PLAY_PAUSE: return CONSUMER_CONTROL_PLAY_PAUSE;
        case ConsumerAction::NEXT_TRACK: return CONSUMER_CONTROL_SCAN_NEXT;
        case ConsumerAction::MUTE: return CONSUMER_CONTROL_MUTE;
        case ConsumerAction::VOLUME_DOWN: return CONSUMER_CONTROL_VOLUME_DECREMENT;
        case ConsumerAction::VOLUME_UP: return CONSUMER_CONTROL_VOLUME_INCREMENT;
        case ConsumerAction::STOP: return CONSUMER_CONTROL_STOP;
        default: return 0;
    }
}

void pressConsumer(ConsumerAction action) {
    if (keyboard_mode == KeyboardMode::USB && usb_mounted) {
        const uint16_t usage = usbConsumerUsage(action);
        if (usage != 0) usbConsumer.press(usage);
        return;
    }
    if (!bleKeyboard.isConnected()) return;

    switch (action) {
        case ConsumerAction::PREVIOUS_TRACK: bleKeyboard.press(KEY_MEDIA_PREVIOUS_TRACK); break;
        case ConsumerAction::PLAY_PAUSE: bleKeyboard.press(KEY_MEDIA_PLAY_PAUSE); break;
        case ConsumerAction::NEXT_TRACK: bleKeyboard.press(KEY_MEDIA_NEXT_TRACK); break;
        case ConsumerAction::MUTE: bleKeyboard.press(KEY_MEDIA_MUTE); break;
        case ConsumerAction::VOLUME_DOWN: bleKeyboard.press(KEY_MEDIA_VOLUME_DOWN); break;
        case ConsumerAction::VOLUME_UP: bleKeyboard.press(KEY_MEDIA_VOLUME_UP); break;
        case ConsumerAction::STOP: bleKeyboard.press(KEY_MEDIA_STOP); break;
        default: break;
    }
}

void pressBinding(const KeyBinding &binding) {
    if (binding.action == KeyActionType::KEYBOARD) {
        pressModifiers(binding.modifiers);
        if (binding.keycode != 0) pressHidKey(binding.keycode);
    } else if (binding.action == KeyActionType::CONSUMER) {
        pressConsumer(static_cast<ConsumerAction>(binding.consumer));
    }
}

/** 根据物理按键状态重建完整 HID 报告，避免修饰键卡住。 */
void rebuildHidState() {
    releaseAllHid();
    for (uint8_t index = 0; index < NUM_KEYS; ++index) {
        if (!key_state[index] || profile_switch_consumed[index]) continue;
        pressBinding(keyProfiles.binding(index));
    }
}

/** 激活键盘上选择的预设，并通知已连接的软件。 */
ProfileResult activateProfileFromKeyboard(uint8_t profileIndex) {
    const uint8_t previousProfile = keyProfiles.activeProfile();
    const ProfileResult result = keyProfiles.setActiveProfile(profileIndex);
    if (result == ProfileResult::OK && keyProfiles.activeProfile() != previousProfile) {
        oledTwin.notifyActiveProfileChanged();
    }
    return result;
}

void handleKeyPress(uint8_t index) {
    const KeyBinding &binding = keyProfiles.binding(index);
    if (binding.action == KeyActionType::FN) {
        fn_pressed = true;
        fn_source_key = index;
        rebuildHidState();
        return;
    }

    if (fn_pressed && index < VICO_PROFILE_COUNT) {
        profile_switch_consumed[index] = true;
        const ProfileResult result = activateProfileFromKeyboard(index);
        if (result == ProfileResult::OK) {
            profile_notice_until = millis() + 900;
            Serial.printf("Active profile changed to P%u\n", index + 1);
        }
        rebuildHidState();
        return;
    }

    if (fn_pressed && (index == 5 || index == 6)) {
        profile_switch_consumed[index] = true;
        oledRuntime.cyclePage(index == 5 ? -1 : 1);
        display_dirty = true;
        rebuildHidState();
        return;
    }

    rebuildHidState();
}

void handleKeyRelease(uint8_t index) {
    if (index == fn_source_key) {
        fn_pressed = false;
        fn_source_key = -1;
    }
    profile_switch_consumed[index] = false;
    rebuildHidState();
}

// =============================================================================
// 开机画面与 USB 未连接提示
// =============================================================================

void showSplash() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // 将 Claude 标志居中显示。
    display.drawBitmap(
        (SCREEN_WIDTH - CLAUDE_LOGO_WIDTH) / 2,
        (SCREEN_HEIGHT - CLAUDE_LOGO_HEIGHT) / 2,
        CLAUDE_LOGO_64X32,
        CLAUDE_LOGO_WIDTH,
        CLAUDE_LOGO_HEIGHT,
        SSD1306_WHITE
    );

    commitOledFrame();
}

// =============================================================================
// 通用按键框绘制
// =============================================================================

void drawKeyBox(uint8_t index, bool pressed) {
    // 使用两行四列网格。
    // 每个按键框为 28×13 像素，间距为 2 像素。
    const uint8_t box_w = 28;
    const uint8_t box_h = 13;
    const uint8_t gap = 2;
    const uint8_t start_x = 4;
    const uint8_t start_y = 16;

    uint8_t col = index % 4;
    uint8_t row = index / 4;
    uint8_t x = start_x + col * (box_w + gap);
    uint8_t y = start_y + row * (box_h + gap);

    if (pressed) {
        // 按下：填充按键框并反转标签颜色。
        display.fillRect(x, y, box_w, box_h, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
    } else {
        // 释放：绘制空心按键框。
        display.drawRect(x, y, box_w, box_h, SSD1306_WHITE);
        display.setTextColor(SSD1306_WHITE);
    }

    // 将按键标签居中。
    display.setTextSize(1);
    // 将 6×8 字体标签居中放入 28×13 按键框。
    char label[5] = {};
    KeyProfileManager::labelForBinding(
        keyProfiles.binding(index),
        label,
        sizeof(label)
    );
    const uint8_t label_width = strlen(label) * 6;
    uint8_t text_x = x + (box_w - label_width) / 2;
    uint8_t text_y = y + (box_h - 8) / 2;
    display.setCursor(text_x, text_y);
    display.print(label);
}

void renderUsbConnectionPrompt() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(40, 0);
    display.print("USB MODE");
    display.setCursor(104, 0);
    if (batteryMonitor.valid()) display.printf("%3u%%", batteryMonitor.percent());
    else display.print(" --%");
    display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

    // 使用四帧动画让 USB 插头逐渐靠近主机端口。
    const uint8_t plug_x = 18 + usb_prompt_frame * 16;
    const uint8_t center_y = 31;

    display.drawLine(0, center_y, plug_x, center_y, SSD1306_WHITE);
    display.drawRect(plug_x, 24, 14, 15, SSD1306_WHITE);
    display.drawLine(plug_x + 14, 27, plug_x + 19, 27, SSD1306_WHITE);
    display.drawLine(plug_x + 14, 35, plug_x + 19, 35, SSD1306_WHITE);

    // 主机侧 USB 端口。
    display.drawRoundRect(96, 19, 31, 25, 3, SSD1306_WHITE);
    display.drawRect(103, 26, 17, 11, SSD1306_WHITE);
    display.drawLine(107, 29, 116, 29, SSD1306_WHITE);
    display.drawLine(107, 34, 116, 34, SSD1306_WHITE);

    display.setCursor(31, 54);
    display.print("PLUG IN USB");
    commitOledFrame();
}

// =============================================================================
// 设备端设置菜单
// =============================================================================
// KEY2/KEY6 上下移动，KEY4 确认，KEY5 返回，KEY8 退出。设置修改后立即写入
// DeviceSettings，并通过 Vendor HID/GATT 通知桌面软件保持双向一致。

void applyOledSettings() {
    const uint8_t contrast = static_cast<uint8_t>(
        deviceSettings.data().oledBrightness * 255UL / 100UL
    );
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(contrast);
}

void applyRgbSettings() {
    const auto &settings = deviceSettings.data();
    rbgLedSetEffect(settings.rgbEffect);
    rbgLedSetBrightness(settings.rgbBrightness);
    rbgLedSetSpeed(settings.rgbSpeed);
    rbgLedSetColor(settings.rgbRed, settings.rgbGreen, settings.rgbBlue);
    rbgLedSetEnabled(settings.rgbEnabled);
}

uint8_t settingsItemCount(SettingsScreen screen) {
    switch (screen) {
        case SettingsScreen::MAIN: return 8;
        case SettingsScreen::PROFILE: return 6;
        case SettingsScreen::OLED: return 6;
        case SettingsScreen::RGB: return 5;
        case SettingsScreen::CONNECTION: return 4;
        case SettingsScreen::DEVICE_INFO: return 10;
        case SettingsScreen::FACTORY_RESET: return 2;
        case SettingsScreen::KEY_TEST: return 0;
    }
    return 0;
}

const char *settingsTitle(SettingsScreen screen) {
    switch (screen) {
        case SettingsScreen::MAIN: return "VICO SETTINGS";
        case SettingsScreen::PROFILE: return "PROFILE";
        case SettingsScreen::OLED: return "OLED";
        case SettingsScreen::RGB: return "RGB LIGHT";
        case SettingsScreen::CONNECTION: return "CONNECTION";
        case SettingsScreen::KEY_TEST: return "KEY TEST";
        case SettingsScreen::DEVICE_INFO: return "DEVICE INFO";
        case SettingsScreen::FACTORY_RESET: return "FACTORY RESET";
    }
    return "SETTINGS";
}

void settingsItemLabel(
    SettingsScreen screen,
    uint8_t index,
    char *destination,
    size_t destinationSize
) {
    static const char *const MAIN_ITEMS[] = {
        "PROFILE", "OLED", "RGB LIGHT", "CONNECTION",
        "KEY TEST", "DEVICE INFO", "FACTORY RESET", "EXIT",
    };
    static const char *const SLEEP_LABELS[] = {
        "ALWAYS ON", "1 MIN", "5 MIN", "10 MIN",
    };
    static const char *const PAGE_LABELS[] = {
        "BRAND", "CODING", "SYSTEM", "CLOCK", "DEVICE", "PIXEL ART",
    };

    destination[0] = '\0';
    switch (screen) {
        case SettingsScreen::MAIN:
            snprintf(destination, destinationSize, "%s", MAIN_ITEMS[index]);
            break;
        case SettingsScreen::PROFILE:
            if (index < VICO_PROFILE_COUNT) {
                snprintf(
                    destination,
                    destinationSize,
                    "P%u%s",
                    index + 1,
                    index == keyProfiles.activeProfile() ? "  ACTIVE" : ""
                );
            } else {
                snprintf(destination, destinationSize, "BACK");
            }
            break;
        case SettingsScreen::OLED:
            if (index == 0) snprintf(destination, destinationSize, "BRIGHTNESS %u%%", deviceSettings.data().oledBrightness);
            if (index == 1) snprintf(destination, destinationSize, "SLEEP %s", SLEEP_LABELS[deviceSettings.data().oledSleepOption]);
            if (index == 2) snprintf(destination, destinationSize, "TWIN %s", deviceSettings.data().oledTwinEnabled ? "ON" : "OFF");
            if (index == 3) snprintf(destination, destinationSize, "PAGE %s", PAGE_LABELS[deviceSettings.data().oledPage]);
            if (index == 4) snprintf(destination, destinationSize, "CLAUDE AUTO %s", deviceSettings.data().oledAutoClaude ? "ON" : "OFF");
            if (index == 5) snprintf(destination, destinationSize, "BACK");
            break;
        case SettingsScreen::RGB:
            if (index == 0) snprintf(destination, destinationSize, "EFFECT %s", rbgLedGetEffectName());
            if (index == 1) snprintf(destination, destinationSize, "BRIGHTNESS %u%%", deviceSettings.data().rgbBrightness);
            if (index == 2) snprintf(destination, destinationSize, "SPEED %u%%", deviceSettings.data().rgbSpeed);
            if (index == 3) snprintf(destination, destinationSize, "LIGHT %s", deviceSettings.data().rgbEnabled ? "ON" : "OFF");
            if (index == 4) snprintf(destination, destinationSize, "BACK");
            break;
        case SettingsScreen::CONNECTION:
            if (index == 0) snprintf(destination, destinationSize, "MODE %s", keyboard_mode == KeyboardMode::USB ? "USB" : "BLE");
            if (index == 1) snprintf(destination, destinationSize, "USB %s", usb_mounted ? "CONNECTED" : "NOT CONNECTED");
            if (index == 2) snprintf(destination, destinationSize, "BLE %s", bleKeyboard.isConnected() ? "CONNECTED" : "NOT CONNECTED");
            if (index == 3) snprintf(destination, destinationSize, "GPIO38 %s", mode_switch_raw_usb ? "HIGH" : "LOW");
            break;
        case SettingsScreen::DEVICE_INFO:
            if (index == 0) snprintf(destination, destinationSize, "FW 0.7.0");
            if (index == 1) snprintf(destination, destinationSize, "PROTOCOL 2");
            if (index == 2) snprintf(destination, destinationSize, "PROFILE P%u", keyProfiles.activeProfile() + 1);
            if (index == 3) snprintf(destination, destinationSize, "MODE %s", keyboard_mode == KeyboardMode::USB ? "USB" : "BLE");
            if (index == 4) snprintf(destination, destinationSize, "LINK %s", (usb_mounted || bleKeyboard.isConnected()) ? "CONNECTED" : "OFFLINE");
            if (index == 5) snprintf(destination, destinationSize, "USB 3343:83CF");
            if (index == 6) snprintf(destination, destinationSize, "BLE VICO KEYBOARD");
            if (index == 7) snprintf(destination, destinationSize, "BATTERY %s", batteryMonitor.valid() ? "OK" : "NOT FOUND");
            if (index == 8) {
                if (batteryMonitor.valid()) snprintf(destination, destinationSize, "BAT %u%% %uMV", batteryMonitor.percent(), batteryMonitor.millivolts());
                else snprintf(destination, destinationSize, "BAT --%% ----MV");
            }
            if (index == 9) snprintf(destination, destinationSize, "NVS OK");
            break;
        case SettingsScreen::FACTORY_RESET:
            snprintf(destination, destinationSize, "%s", index == 0 ? "NO - CANCEL" : "YES - RESET ALL");
            break;
        case SettingsScreen::KEY_TEST:
            break;
    }
}

void drawSettingsList() {
    const uint8_t count = settingsItemCount(settingsMenu.screen);
    const uint8_t first = (settingsMenu.selection / 4) * 4;
    for (uint8_t row = 0; row < 4 && first + row < count; ++row) {
        const uint8_t item = first + row;
        const uint8_t y = 13 + row * 13;
        const bool selected = item == settingsMenu.selection;
        if (selected) {
            display.fillRect(0, y, 128, 12, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
        } else {
            display.setTextColor(SSD1306_WHITE);
        }

        char label[22] = {};
        settingsItemLabel(settingsMenu.screen, item, label, sizeof(label));
        display.setCursor(3, y + 2);
        display.print(selected ? "> " : "  ");
        display.print(label);
    }
}

void renderSettingsMenu() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print(settingsTitle(settingsMenu.screen));
    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    if (settingsMenu.screen == SettingsScreen::KEY_TEST) {
        for (uint8_t index = 0; index < NUM_KEYS; ++index) {
            drawKeyBox(index, key_state[index]);
        }
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 54);
        display.print("K5 BACK  K8 EXIT");
    } else {
        drawSettingsList();
    }
    commitOledFrame();
}

void openSettingsScreen(SettingsScreen screen) {
    settingsMenu.screen = screen;
    settingsMenu.selection = 0;
    display_dirty = true;
}

void enterSettingsMenu() {
    releaseAllHid();
    wakeOled();
    settingsMenu.active = true;
    settingsMenu.waitForRelease = true;
    settingsMenu.screen = SettingsScreen::MAIN;
    settingsMenu.selection = 0;
    profile_notice_until = 0;
    display_dirty = true;
    Serial.println("Settings menu opened");
}

void exitSettingsMenu() {
    releaseAllHid();
    settingsMenu.active = false;
    settingsMenu.waitForRelease = false;
    settingsMenu.screen = SettingsScreen::MAIN;
    settingsMenu.selection = 0;
    display_dirty = true;
    last_user_activity_at = millis();
    Serial.println("Settings menu closed");
}

void settingsBack() {
    if (settingsMenu.screen == SettingsScreen::MAIN) {
        exitSettingsMenu();
    } else {
        openSettingsScreen(SettingsScreen::MAIN);
    }
}

uint8_t nextQuarterStep(uint8_t value) {
    return value >= 100 ? 25 : value + 25;
}

void selectSettingsItem() {
    auto &settings = deviceSettings.edit();

    if (settingsMenu.screen == SettingsScreen::MAIN) {
        switch (settingsMenu.selection) {
            case 0: openSettingsScreen(SettingsScreen::PROFILE); return;
            case 1: openSettingsScreen(SettingsScreen::OLED); return;
            case 2: openSettingsScreen(SettingsScreen::RGB); return;
            case 3: openSettingsScreen(SettingsScreen::CONNECTION); return;
            case 4: openSettingsScreen(SettingsScreen::KEY_TEST); return;
            case 5: openSettingsScreen(SettingsScreen::DEVICE_INFO); return;
            case 6: openSettingsScreen(SettingsScreen::FACTORY_RESET); return;
            case 7: exitSettingsMenu(); return;
        }
    }

    if (settingsMenu.screen == SettingsScreen::PROFILE) {
        if (settingsMenu.selection < VICO_PROFILE_COUNT) {
            activateProfileFromKeyboard(settingsMenu.selection);
        } else {
            settingsBack();
        }
    } else if (settingsMenu.screen == SettingsScreen::OLED) {
        if (settingsMenu.selection == 0) {
            settings.oledBrightness = nextQuarterStep(settings.oledBrightness);
            applyOledSettings();
        } else if (settingsMenu.selection == 1) {
            settings.oledSleepOption = (settings.oledSleepOption + 1) % 4;
        } else if (settingsMenu.selection == 2) {
            settings.oledTwinEnabled = !settings.oledTwinEnabled;
        } else if (settingsMenu.selection == 3) {
            settings.oledPage = (settings.oledPage + 1) % 6;
            oledRuntime.setPageSettings(static_cast<OledPage>(settings.oledPage), settings.oledAutoClaude);
        } else if (settingsMenu.selection == 4) {
            settings.oledAutoClaude = !settings.oledAutoClaude;
            oledRuntime.setPageSettings(static_cast<OledPage>(settings.oledPage), settings.oledAutoClaude);
        } else {
            settingsBack();
        }
        deviceSettings.save();
    } else if (settingsMenu.screen == SettingsScreen::RGB) {
        if (settingsMenu.selection == 0) {
            rbgLedNextEffect();
            settings.rgbEffect = rbgLedGetEffect();
        } else if (settingsMenu.selection == 1) {
            settings.rgbBrightness = nextQuarterStep(settings.rgbBrightness);
        } else if (settingsMenu.selection == 2) {
            settings.rgbSpeed = settings.rgbSpeed >= 200 ? 50 : settings.rgbSpeed + 50;
        } else if (settingsMenu.selection == 3) {
            settings.rgbEnabled = !settings.rgbEnabled;
        } else {
            settingsBack();
        }
        applyRgbSettings();
        deviceSettings.save();
    } else if (settingsMenu.screen == SettingsScreen::CONNECTION ||
               settingsMenu.screen == SettingsScreen::DEVICE_INFO) {
        settingsBack();
    } else if (settingsMenu.screen == SettingsScreen::FACTORY_RESET) {
        if (settingsMenu.selection == 1) {
            const uint8_t previousProfile = keyProfiles.activeProfile();
            keyProfiles.factoryReset();
            if (keyProfiles.activeProfile() != previousProfile) {
                oledTwin.notifyActiveProfileChanged();
            }
            deviceSettings.factoryReset();
            oledRuntime.configure(
                static_cast<OledPage>(deviceSettings.data().oledPage),
                deviceSettings.data().oledAutoClaude
            );
            applyOledSettings();
            applyRgbSettings();
            Serial.println("Factory settings restored");
        }
        settingsBack();
    }
    display_dirty = true;
}

void handleSettingsKeyPress(uint8_t index) {
    // 固定菜单控制键有意忽略当前用户预设。
    if (index == 7) { // KEY8：退出
        exitSettingsMenu();
        return;
    }
    if (index == 4) { // KEY5：返回
        settingsBack();
        return;
    }
    if (settingsMenu.screen == SettingsScreen::KEY_TEST) {
        display_dirty = true;
        return;
    }

    const uint8_t count = settingsItemCount(settingsMenu.screen);
    if (index == 5 && count > 0) { // KEY6：向上
        settingsMenu.selection = settingsMenu.selection == 0
            ? count - 1
            : settingsMenu.selection - 1;
    } else if (index == 1 && count > 0) { // KEY2：向下
        settingsMenu.selection = (settingsMenu.selection + 1) % count;
    } else if (index == 3) { // KEY4：确认
        selectSettingsItem();
    }
    display_dirty = true;
}

// =============================================================================
// OLED 运行时页面绘制
// =============================================================================
// 所有页面只读取 OledRuntimeSnapshot，不直接访问 BLE/USB 接收缓冲区。
// Coding 页面动画使用 millis() 计算当前帧，不维护额外动画队列。

const char *claudeStateDisplay(ClaudeState state) {
    switch (state) {
        case ClaudeState::READY: return "READY";
        case ClaudeState::WORKING: return "THINK";
        case ClaudeState::TOOL: return "TOOL";
        case ClaudeState::WAITING: return "INPUT";
        case ClaudeState::DONE: return "DONE";
        case ClaudeState::ERROR_STATE: return "ERROR";
        default: return "OFFLINE";
    }
}

const char *claudeActivityHint(ClaudeState state) {
    switch (state) {
        case ClaudeState::READY: return "WAITING FOR PROMPT";
        case ClaudeState::WORKING: return "CLAUDE IS THINKING";
        case ClaudeState::TOOL: return "RUNNING TOOL";
        case ClaudeState::WAITING: return "USER INPUT NEEDED";
        case ClaudeState::DONE: return "TASK FINISHED";
        case ClaudeState::ERROR_STATE: return "CHECK CLAUDE LOG";
        default: return "START CLAUDE CODE";
    }
}

void printClipped(const char *text, uint8_t maximumCharacters) {
    if (text == nullptr) return;
    for (uint8_t index = 0; text[index] != '\0' && index < maximumCharacters; ++index) {
        display.write(text[index]);
    }
}

/** @brief 在指定位置绘制固定四字符宽度的真实电池百分比。 */
void drawBatteryPercent(uint8_t x, uint8_t y) {
    display.setCursor(x, y);
    if (batteryMonitor.valid()) display.printf("%3u%%", batteryMonitor.percent());
    else display.print(" --%");
}

void drawClaudeOrbitPixel(uint8_t position) {
    constexpr uint8_t x = 94;
    constexpr uint8_t y = 11;
    constexpr uint8_t width = 34;
    constexpr uint8_t height = 20;
    if (position < width) {
        display.drawPixel(x + position, y, SSD1306_WHITE);
    } else if (position < width + height - 1) {
        display.drawPixel(x + width - 1, y + position - width + 1, SSD1306_WHITE);
    } else if (position < width * 2 + height - 2) {
        display.drawPixel(x + width - 2 - (position - width - height + 1), y + height - 1, SSD1306_WHITE);
    } else {
        display.drawPixel(x, y + height - 2 - (position - width * 2 - height + 2), SSD1306_WHITE);
    }
}

void drawClaudeThinkingAnimation(ClaudeState state) {
    if (state != ClaudeState::WORKING) return;
    constexpr uint8_t perimeter = 2 * (34 + 20) - 4;
    constexpr uint8_t segmentLength = 14;
    // 128×64 帧是参考项目 128×32 帧的两倍，降低刷新频率以减少 I2C 占用。
    const uint8_t frame = static_cast<uint8_t>(((millis() / 180) * 6) % perimeter);
    for (uint8_t index = 0; index < segmentLength; ++index) {
        drawClaudeOrbitPixel((frame + index) % perimeter);
    }
}

void drawRuntimeHeader(const char *title) {
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(title);
    drawBatteryPercent(104, 0);
    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
}

void drawMetricRow(const char *label, uint8_t value, uint8_t y) {
    display.setCursor(0, y);
    display.print(label);
    if (value <= 100) {
        display.setCursor(24, y);
        display.printf("%3u", value);
        display.drawRect(47, y, 80, 8, SSD1306_WHITE);
        const uint8_t width = static_cast<uint8_t>(value * 76UL / 100UL);
        if (width > 0) display.fillRect(49, y + 2, width, 4, SSD1306_WHITE);
    } else {
        display.setCursor(24, y);
        display.print(" --");
        display.drawRect(47, y, 80, 8, SSD1306_WHITE);
    }
}

void renderBrandRuntimePage() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(1, 1);
    display.print(keyboard_mode == KeyboardMode::USB ? "USB" : "BLE");
    drawBatteryPercent(104, 1);
    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(4, 15);
    display.print("VICO");
    display.setTextSize(1);
    display.setCursor(5, 40);
    display.print("CREATE YOUR FLOW");

    static const uint8_t POINTS[][2] = {
        {83, 28}, {90, 23}, {97, 32}, {104, 20}, {111, 29}, {124, 22},
    };
    for (size_t index = 1; index < sizeof(POINTS) / sizeof(POINTS[0]); ++index) {
        display.drawLine(
            POINTS[index - 1][0], POINTS[index - 1][1],
            POINTS[index][0], POINTS[index][1],
            SSD1306_WHITE
        );
    }
    commitOledFrame();
}

void renderClaudeRuntimePage(const OledRuntimeSnapshot &runtime) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    // Coding 页只保留与 Claude 工作流直接相关的信息。
    display.setCursor(0, 0);
    display.print("CLAUDE CODE");
    display.setCursor(82, 0);
    if (keyboard_mode == KeyboardMode::USB) display.print(usb_mounted ? "USB" : "---");
    else display.print(bleKeyboard.isConnected() ? "BLE" : "ADV");
    drawBatteryPercent(104, 0);
    display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

    // 大状态文字和 Claude 图标形成第一视觉层级；思考时线段沿图标外圈流动。
    display.setTextSize(2);
    display.setCursor(0, 14);
    display.print(claudeStateDisplay(runtime.claudeState));
    display.setTextSize(1);
    display.drawBitmap(96, 13, CLAUDE_LOGO_32X16,
        CLAUDE_LOGO_SMALL_WIDTH, CLAUDE_LOGO_SMALL_HEIGHT, SSD1306_WHITE);
    drawClaudeThinkingAnimation(runtime.claudeState);
    display.drawLine(0, 32, 127, 32, SSD1306_WHITE);

    display.setCursor(0, 35);
    if (runtime.tool[0] != '\0') {
        display.print("TOOL ");
        printClipped(runtime.tool, 15);
    } else {
        printClipped(claudeActivityHint(runtime.claudeState), 21);
    }
    display.setCursor(0, 45);
    printClipped(runtime.text[0] != '\0' ? runtime.text : "Waiting for task", 21);

    display.setCursor(0, 55);
    if (runtime.activeSessions > 0) {
        const uint32_t age = runtime.activityAgeSeconds +
            (runtime.activityAgeReceivedAt == 0 ? 0 : (millis() - runtime.activityAgeReceivedAt) / 1000);
        display.printf("S:%u  LAST %02lu:%02lu", runtime.activeSessions, age / 60, age % 60);
    } else {
        display.print("HOOK READY");
    }
    commitOledFrame();
}

void renderSystemRuntimePage(const OledRuntimeSnapshot &runtime) {
    display.clearDisplay();
    drawRuntimeHeader("SYSTEM MONITOR");
    const bool fresh = runtime.computerOnline && millis() - runtime.receivedAt < 5000;
    drawMetricRow("CPU", fresh ? runtime.cpu : 0xFF, 15);
    drawMetricRow("GPU", fresh ? runtime.gpu : 0xFF, 28);
    drawMetricRow("RAM", fresh ? runtime.memory : 0xFF, 41);
    display.setCursor(0, 55);
    if (fresh && runtime.temperature <= 100) display.printf("TEMP %uC", runtime.temperature);
    else display.print(fresh ? "TEMP --" : "PC OFFLINE");
    display.setCursor(98, 55);
    display.printf("%02u:%02u", runtime.hour, runtime.minute);
    commitOledFrame();
}

void renderClockRuntimePage(const OledRuntimeSnapshot &runtime) {
    display.clearDisplay();
    drawRuntimeHeader("CLOCK");
    if (runtime.receivedAt == 0) {
        display.setCursor(35, 23);
        display.print("SYNC TIME");
        display.setCursor(17, 39);
        display.print("CONNECT VICO APP");
        commitOledFrame();
        return;
    }
    display.setTextSize(3);
    display.setCursor(18, 17);
    display.printf("%02u:%02u", runtime.hour, runtime.minute);
    display.setTextSize(1);
    static const char *const WEEKDAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    display.setCursor(29, 50);
    display.printf("%02u-%02u  %s", runtime.month, runtime.day, WEEKDAYS[runtime.weekday]);
    commitOledFrame();
}

void renderDeviceRuntimePage() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // 顶栏：标题和当前连接模式。
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Vico ESP32-S3");
    display.setCursor(84, 0);
    display.printf("P%u", keyProfiles.activeProfile() + 1);
    display.setCursor(104, 0);
    display.print(keyboard_mode == KeyboardMode::USB ? "USB" :
        (bleKeyboard.isConnected() ? "BLE" : "..."));
    display.drawLine(0, 12, 127, 12, SSD1306_WHITE);
    for (uint8_t index = 0; index < NUM_KEYS; ++index) drawKeyBox(index, key_state[index]);
    display.setCursor(0, 54);
    if (batteryMonitor.valid()) display.printf("BAT %u%%  %uMV", batteryMonitor.percent(), batteryMonitor.millivolts());
    else display.print("BATTERY NOT FOUND");
    commitOledFrame();
}

void renderCustomRuntimePage() {
    display.clearDisplay();
    display.drawBitmap(0, 0, oledRuntime.customBitmap(), 128, 64, SSD1306_WHITE);
    commitOledFrame();
}

void renderRuntimePage() {
    const OledRuntimeSnapshot runtime = oledRuntime.snapshot();
    switch (oledRuntime.effectivePage(millis())) {
        case OledPage::BRAND: renderBrandRuntimePage(); break;
        case OledPage::CLAUDE: renderClaudeRuntimePage(runtime); break;
        case OledPage::SYSTEM: renderSystemRuntimePage(runtime); break;
        case OledPage::CLOCK: renderClockRuntimePage(runtime); break;
        case OledPage::DEVICE: renderDeviceRuntimePage(); break;
        case OledPage::CUSTOM: renderCustomRuntimePage(); break;
    }
}

// =============================================================================
// OLED 顶层页面调度
// =============================================================================
// 设置菜单、USB 插线提示和预设切换提示拥有更高显示优先级；其余时间交给
// OledRuntime::effectivePage() 决定品牌、Coding、性能等运行时页面。

void renderKeyStatus() {
    if (settingsMenu.active) {
        renderSettingsMenu();
        return;
    }
    if (keyboard_mode == KeyboardMode::USB && !usb_mounted) {
        renderUsbConnectionPrompt();
        return;
    }

    if (profile_notice_until != 0 &&
        static_cast<int32_t>(profile_notice_until - millis()) > 0) {
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(31, 13);
        display.print("ACTIVE PROFILE");
        display.setTextSize(3);
        display.setCursor(46, 30);
        display.printf("P%u", keyProfiles.activeProfile() + 1);
        commitOledFrame();
        return;
    }

    renderRuntimePage();
}

// =============================================================================
// Arduino 初始化入口
// =============================================================================

/** @brief 初始化串口、设置、OLED、按键、灯带以及开机选择的 HID 模式。 */
void setup() {
    Serial.begin(115200);
    pinMode(MODE_SELECT_PIN, INPUT_PULLDOWN);
    delay(10);
    keyboard_mode = digitalRead(MODE_SELECT_PIN) == HIGH
        ? KeyboardMode::USB
        : KeyboardMode::BLE;
    mode_switch_raw_usb = keyboard_mode == KeyboardMode::USB;
    mode_switch_changed_at = millis();

    Serial.printf(
        "=== Vico Keyboard ESP32-S3 (%s mode) ===\n",
        keyboard_mode == KeyboardMode::USB ? "USB" : "BLE"
    );

    // 初始化 I2C 和 OLED。
    Wire.begin(I2C_SDA, I2C_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[FAIL] SSD1306 init failed!");
        while (true) delay(1000);
    }
    Serial.println("SSD1306 init OK");

    deviceSettings.begin();
    oledRuntime.configure(
        static_cast<OledPage>(deviceSettings.data().oledPage),
        deviceSettings.data().oledAutoClaude
    );
    applyOledSettings();
    last_user_activity_at = millis();

    // 在键盘接口启动前加载五套可编辑预设。
    keyProfiles.begin();

    // 初始化按键输入引脚。
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        pinMode(KEY_PINS[i], INPUT_PULLUP);
    }

    // 初始化 RGB 灯带。
    rbgLedInit();
    applyRgbSettings();

    // GPIO14通过100K/100K分压读取单节锂电池；初值同时提供给BLE和USB协议。
    batteryMonitor.begin();
    bleKeyboard.setBatteryLevel(batteryMonitor.valid() ? batteryMonitor.percent() : 0);
    oledTwin.notifyBatteryChanged(
        batteryMonitor.valid() ? batteryMonitor.percent() : 0,
        batteryMonitor.valid() ? batteryMonitor.millivolts() : 0
    );

    startKeyboardInterface(keyboard_mode);

    // 跳过 Claude Code 开机画面，直接显示键盘状态。
    renderKeyStatus();
    display_dirty = false;
}

// =============================================================================
// Arduino 主循环
// =============================================================================
// 执行顺序经过刻意安排：先推进灯效和连接状态，再扫描/发送按键，之后才绘制
// OLED 和发送数字孪生分片，使显示与后台通信不会抢在键盘输入之前。

/** @brief 非阻塞调度连接、按键、设置、OLED、RGB 和数字孪生任务。 */
void loop() {
    updateModeSwitch();
    unsigned long now = millis();
    updateUsbConnectionState(now);

    if (profile_notice_until != 0 &&
        static_cast<int32_t>(now - profile_notice_until) >= 0) {
        profile_notice_until = 0;
        display_dirty = true;
    }

    // 非阻塞更新 RGB 灯效。
    rbgLedUpdate();

    // 扫描全部按键并进行消抖。
    bool changed = false;
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        bool raw = (digitalRead(KEY_PINS[i]) == LOW);

        if (raw != key_state[i] && (now - last_change[i]) >= DEBOUNCE_MS) {
            key_state[i] = raw;
            last_change[i] = now;
            changed = true;
        }
    }

    if (changed) wakeOled();

    // KEY4 + KEY8 是物理系统组合键，不受用户映射影响。
    if (!settingsMenu.active && key_state[3] && key_state[7] &&
        (!last_reported[3] || !last_reported[7])) {
        enterSettingsMenu();
    }

    if (changed) {
        if (settingsMenu.active) {
            if (settingsMenu.waitForRelease) {
                bool anyPressed = false;
                for (bool pressed : key_state) anyPressed |= pressed;
                settingsMenu.waitForRelease = anyPressed;
            } else {
                for (uint8_t i = 0; i < NUM_KEYS; i++) {
                    if (key_state[i] && !last_reported[i]) {
                        handleSettingsKeyPress(i);
                        if (!settingsMenu.active) break;
                    }
                }
            }
        } else {
            // 正常模式通过 USB 或蓝牙 HID 发送当前预设。
            for (uint8_t i = 0; i < NUM_KEYS; i++) {
                if (key_state[i] && !last_reported[i]) {
                    handleKeyPress(i);
                } else if (!key_state[i] && last_reported[i]) {
                    handleKeyRelease(i);
                }
            }
        }

        for (uint8_t i = 0; i < NUM_KEYS; i++) {
            last_reported[i] = key_state[i];
        }
        display_dirty = true;
    }

    // 按键扫描和HID发送完成后再做低频ADC采样，避免电池测量增加输入路径延迟。
    // 只有百分比、有效状态或至少10mV的显示值变化时，才刷新OLED和通知主机。
    if (batteryMonitor.update(now)) {
        display_dirty = true;
        if (keyboard_mode == KeyboardMode::BLE) {
            bleKeyboard.setBatteryLevel(batteryMonitor.valid() ? batteryMonitor.percent() : 0);
        }
        oledTwin.notifyBatteryChanged(
            batteryMonitor.valid() ? batteryMonitor.percent() : 0,
            batteryMonitor.valid() ? batteryMonitor.millivolts() : 0
        );
    }

    updateOledSleep(now);

    if (oledRuntime.takeDirty()) display_dirty = true;
    OledPage changedPage = OledPage::BRAND;
    bool changedAutoClaude = true;
    if (oledRuntime.takeSettingsChange(changedPage, changedAutoClaude)) {
        auto &settings = deviceSettings.edit();
        settings.oledPage = static_cast<uint8_t>(changedPage);
        settings.oledAutoClaude = changedAutoClaude;
        deviceSettings.save();
        oledTwin.notifyRuntimeSettingsChanged(changedPage, changedAutoClaude);
        display_dirty = true;
    }

    // USB Vendor HID 与蓝牙 GATT 共用同一份 RGB 设置包。
    // 灯效立即应用；停止调整 750ms 后再合并写入 NVS，避免频繁擦写 Flash。
    static bool rgbSettingsSavePending = false;
    static uint32_t rgbSettingsChangedAt = 0;
    RgbRuntimeSettings changedRgb = {};
    if (oledRuntime.takeRgbSettingsChange(changedRgb)) {
        auto &settings = deviceSettings.edit();
        settings.rgbEffect = changedRgb.effect;
        settings.rgbBrightness = changedRgb.brightness;
        settings.rgbSpeed = changedRgb.speed;
        settings.rgbEnabled = changedRgb.enabled;
        settings.rgbRed = changedRgb.red;
        settings.rgbGreen = changedRgb.green;
        settings.rgbBlue = changedRgb.blue;
        applyRgbSettings();
        rgbSettingsSavePending = true;
        rgbSettingsChangedAt = now;
    }
    if (rgbSettingsSavePending && now - rgbSettingsChangedAt >= 750) {
        deviceSettings.save();
        rgbSettingsSavePending = false;
    }

    // Coding 页面思考动画约 5.5 FPS；其他状态每秒刷新一次 LAST 时间。
    static uint32_t lastClaudeAnimationAt = 0;
    const OledRuntimeSnapshot animationRuntime = oledRuntime.snapshot();
    if (!settingsMenu.active && oledRuntime.effectivePage(now) == OledPage::CLAUDE) {
        const uint32_t refreshInterval = animationRuntime.claudeState == ClaudeState::WORKING ? 180 : 1000;
        if (now - lastClaudeAnimationAt >= refreshInterval) {
            lastClaudeAnimationAt = now;
            display_dirty = true;
        }
    }

    // 蓝牙未连接时定期刷新界面。
    if (keyboard_mode == KeyboardMode::BLE && !bleKeyboard.isConnected() &&
        !settingsMenu.active) {
        static unsigned long last_draw = 0;
        if (now - last_draw > 500) {
            last_draw = now;
            renderKeyStatus();
        }
        return;
    }

    // 按键状态变化后刷新 OLED。
    if (display_dirty) {
        display_dirty = false;
        renderKeyStatus();
    }

    // 仅在按键报告和 OLED 绘制完成后处理数字孪生流量。
    // 每次循环中，update() 最多发送一份 63 字节 Vendor HID 报告。
    oledTwin.update(keyboard_mode == KeyboardMode::USB && usb_mounted);

    if (oledTwin.takeProfileChanged()) {
        releaseAllHid();
        resetKeyTracking();
        profile_notice_until = millis() + 900;
        display_dirty = true;
    }
}
