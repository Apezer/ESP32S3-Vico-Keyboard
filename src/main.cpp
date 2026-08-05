/**
 * ESP32-S3 USB/BLE Dual-Mode Mini Keyboard
 *
 * Hardware: DFRobot FireBeetle2 ESP32-S3 + 0.96-inch OLED (SSD1306, I2C)
 *
 * Wiring:
 *   OLED SDA -> GPIO 10
 *   OLED SCL -> GPIO 9
 *   OLED VCC -> 3.3V
 *   OLED GND -> GND
 *
 *   K1 -> GPIO 18  (Left)
 *   K2 -> GPIO 17  (Down)
 *   K3 -> GPIO 16  (Right)
 *   K4 -> GPIO 15  (Enter)
 *   K5 -> GPIO 5   (Backspace)
 *   K6 -> GPIO 6   (Up)
 *   K7 -> GPIO 7   (Ctrl + Win)
 *   K8 -> GPIO 4   (Fn, internal layer key)
 *
 *   WS2812B DIN -> GPIO 8
 *   Mode switch common -> GPIO 35
 *     LOW/GND  = BLE mode
 *     HIGH/3V3 = USB mode
 *
 * Dependencies (configured in platformio.ini):
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
#include "font.h"
#include "device_settings.h"
#include "key_profiles.h"
#include "oled_twin.h"
#include "rbg_led.h"

#if !ARDUINO_USB_MODE
#error "Native USB must be enabled with ARDUINO_USB_MODE=1."
#endif

// ===== Hardware configuration =====
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDR       0x3C

#define I2C_SDA         10
#define I2C_SCL         9
#define MODE_SELECT_PIN 35

#define NUM_KEYS        8
#define DEBOUNCE_MS     10
#define MODE_DEBOUNCE_MS 50
#define USB_PROMPT_FRAME_MS 250

const uint8_t KEY_PINS[VICO_KEY_COUNT] = {18, 17, 16, 15, 5, 6, 7, 4};

enum class KeyboardMode : uint8_t {
    BLE,
    USB,
};

// ===== Key state =====
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

struct SettingsMenuState {
    bool active = false;
    bool waitForRelease = false;
    SettingsScreen screen = SettingsScreen::MAIN;
    uint8_t selection = 0;
};

static SettingsMenuState settingsMenu;

// ===== Device objects =====
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
BleKeyboard bleKeyboard("Vico Keyboard", "Apezer", 100);
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

/** Push the framebuffer to the physical OLED, then snapshot that exact frame. */
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

void startKeyboardInterface(KeyboardMode mode) {
    if (mode == KeyboardMode::USB) {
        usb_mounted = false;
        if (!usb_started) {
            // HID interfaces must be registered before native USB starts.
            USB.manufacturerName("Apezer");
            USB.productName("Vico Keyboard");
            usbKeyboard.begin();
            usbConsumer.begin();
            oledTwin.begin(&keyProfiles);
            usb_started = USB.begin();
        } else {
            tud_connect();
        }
    } else {
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

// ===== Key event handling =====
void pressHidKey(uint8_t keycode) {
    if (keyboard_mode == KeyboardMode::USB && usb_mounted) {
        // USBHIDKeyboard::press() updates its modifier bitmap for the
        // Arduino-style key codes 0x80-0x87, but then calls pressRaw(0).
        // In the ESP32 Arduino core used by this project, pressRaw(0) exits
        // without sending a report. A modifier-only binding such as
        // Ctrl+GUI would therefore never reach the host.
        //
        // Raw HID modifier usages are 0xE0-0xE7. Sending them through
        // pressRaw() both updates the bitmap and immediately transmits the
        // report. Non-modifier keys keep the library's normal translation.
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

/** Rebuild the complete HID report from physical state to avoid stuck modifiers. */
void rebuildHidState() {
    releaseAllHid();
    for (uint8_t index = 0; index < NUM_KEYS; ++index) {
        if (!key_state[index] || profile_switch_consumed[index]) continue;
        pressBinding(keyProfiles.binding(index));
    }
}

/** Activate a profile selected on the keyboard and notify a connected app. */
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

// ===== Startup screen =====
void showSplash() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // Center the Claude logo.
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

// ===== Individual key-state rendering =====
void drawKeyBox(uint8_t index, bool pressed) {
    // Two-row by four-column grid.
    // Each key box is 28x14 pixels with a 2-pixel gap.
    const uint8_t box_w = 28;
    const uint8_t box_h = 14;
    const uint8_t gap = 2;
    const uint8_t start_x = 4;
    const uint8_t start_y = 18;

    uint8_t col = index % 4;
    uint8_t row = index / 4;
    uint8_t x = start_x + col * (box_w + gap);
    uint8_t y = start_y + row * (box_h + gap);

    if (pressed) {
        // Pressed: fill the box and invert the label.
        display.fillRect(x, y, box_w, box_h, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
    } else {
        // Released: draw an outlined box.
        display.drawRect(x, y, box_w, box_h, SSD1306_WHITE);
        display.setTextColor(SSD1306_WHITE);
    }

    // Center the key label.
    display.setTextSize(1);
    // Center the 6x8 font label inside the 28x14 box.
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
    display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

    // Four animation frames move a USB plug toward the host port.
    const uint8_t plug_x = 18 + usb_prompt_frame * 16;
    const uint8_t center_y = 31;

    display.drawLine(0, center_y, plug_x, center_y, SSD1306_WHITE);
    display.drawRect(plug_x, 24, 14, 15, SSD1306_WHITE);
    display.drawLine(plug_x + 14, 27, plug_x + 19, 27, SSD1306_WHITE);
    display.drawLine(plug_x + 14, 35, plug_x + 19, 35, SSD1306_WHITE);

    // Host-side USB port.
    display.drawRoundRect(96, 19, 31, 25, 3, SSD1306_WHITE);
    display.drawRect(103, 26, 17, 11, SSD1306_WHITE);
    display.drawLine(107, 29, 116, 29, SSD1306_WHITE);
    display.drawLine(107, 34, 116, 34, SSD1306_WHITE);

    display.setCursor(31, 54);
    display.print("PLUG IN USB");
    commitOledFrame();
}

// ===== On-device settings menu =====
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
    rbgLedSetEnabled(settings.rgbEnabled);
}

uint8_t settingsItemCount(SettingsScreen screen) {
    switch (screen) {
        case SettingsScreen::MAIN: return 8;
        case SettingsScreen::PROFILE: return 6;
        case SettingsScreen::OLED: return 4;
        case SettingsScreen::RGB: return 5;
        case SettingsScreen::CONNECTION: return 4;
        case SettingsScreen::DEVICE_INFO: return 8;
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
            if (index == 3) snprintf(destination, destinationSize, "BACK");
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
            if (index == 3) snprintf(destination, destinationSize, "GPIO35 %s", mode_switch_raw_usb ? "HIGH" : "LOW");
            break;
        case SettingsScreen::DEVICE_INFO:
            if (index == 0) snprintf(destination, destinationSize, "FW 0.5.0");
            if (index == 1) snprintf(destination, destinationSize, "PROTOCOL 2");
            if (index == 2) snprintf(destination, destinationSize, "PROFILE P%u", keyProfiles.activeProfile() + 1);
            if (index == 3) snprintf(destination, destinationSize, "MODE %s", keyboard_mode == KeyboardMode::USB ? "USB" : "BLE");
            if (index == 4) snprintf(destination, destinationSize, "LINK %s", (usb_mounted || bleKeyboard.isConnected()) ? "CONNECTED" : "OFFLINE");
            if (index == 5) snprintf(destination, destinationSize, "USB 3343:83CF");
            if (index == 6) snprintf(destination, destinationSize, "BLE VICO KEYBOARD");
            if (index == 7) snprintf(destination, destinationSize, "NVS OK");
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
            applyOledSettings();
            applyRgbSettings();
            Serial.println("Factory settings restored");
        }
        settingsBack();
    }
    display_dirty = true;
}

void handleSettingsKeyPress(uint8_t index) {
    // Fixed menu controls intentionally ignore the active user profile.
    if (index == 7) { // KEY8: exit
        exitSettingsMenu();
        return;
    }
    if (index == 4) { // KEY5: back
        settingsBack();
        return;
    }
    if (settingsMenu.screen == SettingsScreen::KEY_TEST) {
        display_dirty = true;
        return;
    }

    const uint8_t count = settingsItemCount(settingsMenu.screen);
    if (index == 5 && count > 0) { // KEY6: up
        settingsMenu.selection = settingsMenu.selection == 0
            ? count - 1
            : settingsMenu.selection - 1;
    } else if (index == 1 && count > 0) { // KEY2: down
        settingsMenu.selection = (settingsMenu.selection + 1) % count;
    } else if (index == 3) { // KEY4: confirm
        selectSettingsItem();
    }
    display_dirty = true;
}

// ===== Key-state screen rendering =====
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

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // Header: title and current connection mode.
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Vico ESP32-S3");

    display.setCursor(84, 0);
    display.printf("P%u", keyProfiles.activeProfile() + 1);

    display.setCursor(104, 0);
    if (keyboard_mode == KeyboardMode::USB) {
        display.print("USB");
    } else {
        display.print(bleKeyboard.isConnected() ? "BLE" : "...");
    }

    // Divider.
    display.drawLine(0, 12, 127, 12, SSD1306_WHITE);

    // Render all eight key states.
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        drawKeyBox(i, key_state[i]);
    }

    // Footer: GPIO mapping reference.
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 54);
    display.print("18 17 16 15 5 6 7 4");

    commitOledFrame();
}

// ===== setup =====
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

    // Initialize I2C and the OLED.
    Wire.begin(I2C_SDA, I2C_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[FAIL] SSD1306 init failed!");
        while (true) delay(1000);
    }
    Serial.println("SSD1306 init OK");

    deviceSettings.begin();
    applyOledSettings();
    last_user_activity_at = millis();

    // Load all five editable presets before keyboard interfaces start.
    keyProfiles.begin();

    // Initialize key input pins.
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        pinMode(KEY_PINS[i], INPUT_PULLUP);
    }

    // Initialize the RGB strip.
    rbgLedInit();
    applyRgbSettings();

    startKeyboardInterface(keyboard_mode);

    // Skip the Claude Code splash and show the keyboard state immediately.
    renderKeyStatus();
    display_dirty = false;
}

// ===== loop =====
void loop() {
    updateModeSwitch();
    unsigned long now = millis();
    updateUsbConnectionState(now);

    if (profile_notice_until != 0 &&
        static_cast<int32_t>(now - profile_notice_until) >= 0) {
        profile_notice_until = 0;
        display_dirty = true;
    }

    // Update the RGB effect without blocking.
    rbgLedUpdate();

    // Scan and debounce all keys.
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

    // KEY4 + KEY8 is a physical system chord, independent of user mappings.
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
            // Normal mode sends the active profile through USB or BLE HID.
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

    updateOledSleep(now);

    // Refresh periodically while BLE is disconnected.
    if (keyboard_mode == KeyboardMode::BLE && !bleKeyboard.isConnected() &&
        !settingsMenu.active) {
        static unsigned long last_draw = 0;
        if (now - last_draw > 500) {
            last_draw = now;
            renderKeyStatus();
        }
        return;
    }

    // Refresh the OLED after a key-state change.
    if (display_dirty) {
        display_dirty = false;
        renderKeyStatus();
    }

    // Service digital-twin traffic only after key reports and OLED rendering.
    // update() sends at most one 63-byte Vendor HID report per loop pass.
    oledTwin.update(keyboard_mode == KeyboardMode::USB && usb_mounted);

    if (oledTwin.takeProfileChanged()) {
        releaseAllHid();
        resetKeyTracking();
        profile_notice_until = millis() + 900;
        display_dirty = true;
    }
}
