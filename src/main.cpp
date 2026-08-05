/**
 * ESP32-S3 USB/BLE Dual-Mode Mini Keyboard
 *
 * 硬件: DFRobot FireBeetle2 ESP32-S3 + 0.96寸 OLED (SSD1306, I2C)
 *
 * 接线:
 *   OLED SDA -> GPIO 10
 *   OLED SCL -> GPIO 9
 *   OLED VCC -> 3.3V
 *   OLED GND -> GND
 *
 *   K1 -> GPIO 18  (Left)
 *   K2 -> GPIO 17  (Down)
 *   K3 -> GPIO 16  (Right)
 *   K4 -> GPIO 15  (Enter)
 *   K5 -> GPIO 7   (Backspace)
 *   K6 -> GPIO 6   (Up)
 *   K7 -> GPIO 5   (Ctrl + Win)
 *   K8 -> GPIO 4   (Fn, internal layer key)
 *
 *   WS2812B DIN -> GPIO 8
 *   Mode switch common -> GPIO 38
 *     LOW/GND  = BLE mode
 *     HIGH/3V3 = USB mode
 *
 * 依赖库 (platformio.ini 已配置):
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
#include "USBHIDKeyboard.h"
#include "tusb.h"
#include "font.h"
#include "oled_twin.h"
#include "rbg_led.h"

#if !ARDUINO_USB_MODE
#error "Native USB must be enabled with ARDUINO_USB_MODE=1."
#endif

// ===== 硬件配置 =====
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

enum class KeyboardMode : uint8_t {
    BLE,
    USB,
};

// ===== 按键配置 =====
// Fn is handled inside the firmware and deliberately sends no HID keycode.
enum class KeyAction : uint8_t {
    HID_KEY,
    CTRL_WIN,
    FN,
};

struct KeyBinding {
    uint8_t pin;
    KeyAction action;
    uint8_t keycode;
    const char *oled_label;
};

const KeyBinding DEFAULT_KEY_BINDINGS[NUM_KEYS] = {
    {18, KeyAction::HID_KEY, KEY_LEFT_ARROW,  "<"},
    {17, KeyAction::HID_KEY, KEY_DOWN_ARROW,  "DN"},
    {16, KeyAction::HID_KEY, KEY_RIGHT_ARROW, ">"},
    {15, KeyAction::HID_KEY, KEY_RETURN,      "ENT"},
    { 7, KeyAction::HID_KEY, KEY_BACKSPACE,   "BSP"},
    { 6, KeyAction::HID_KEY, KEY_UP_ARROW,    "UP"},
    { 5, KeyAction::CTRL_WIN, 0,               "C+W"},
    { 4, KeyAction::FN,       0,               "FN"},
};

// ===== 按键状态 =====
static bool key_state[NUM_KEYS] = {false};
static bool last_reported[NUM_KEYS] = {false};
static unsigned long last_change[NUM_KEYS] = {0};
static bool fn_pressed = false;
static bool display_dirty = true;
static KeyboardMode keyboard_mode = KeyboardMode::BLE;
static bool mode_switch_raw_usb = false;
static unsigned long mode_switch_changed_at = 0;
static bool usb_started = false;
static bool usb_mounted = false;
static uint8_t usb_prompt_frame = 0;
static unsigned long usb_prompt_frame_at = 0;

// ===== 创建对象 =====
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
BleKeyboard bleKeyboard("Vico Keyboard", "Apezer", 100);
USBHIDKeyboard usbKeyboard;
OledTwinTransport oledTwin;

/** Push the framebuffer to the physical OLED, then snapshot that exact frame. */
void commitOledFrame() {
    display.display();
    oledTwin.captureFrame(display.getBuffer(), OledTwinTransport::FRAME_BYTES);
}

void resetKeyTracking() {
    const unsigned long now = millis();
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        key_state[i] = false;
        last_reported[i] = false;
        last_change[i] = now;
    }
    fn_pressed = false;
}

void startKeyboardInterface(KeyboardMode mode) {
    if (mode == KeyboardMode::USB) {
        usb_mounted = false;
        if (!usb_started) {
            // HID interfaces must be registered before native USB starts.
            USB.manufacturerName("Apezer");
            USB.productName("Vico Keyboard");
            usbKeyboard.begin();
            oledTwin.begin();
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

// ===== 按键事件处理 =====
void pressHidKey(uint8_t keycode) {
    if (keyboard_mode == KeyboardMode::USB && usb_mounted) {
        usbKeyboard.press(keycode);
    } else if (bleKeyboard.isConnected()) {
        bleKeyboard.press(keycode);
    }
}

void releaseHidKey(uint8_t keycode) {
    if (keyboard_mode == KeyboardMode::USB && usb_mounted) {
        usbKeyboard.release(keycode);
    } else if (bleKeyboard.isConnected()) {
        bleKeyboard.release(keycode);
    }
}

void handleKeyPress(uint8_t index) {
    const KeyBinding &binding = DEFAULT_KEY_BINDINGS[index];

    switch (binding.action) {
        case KeyAction::HID_KEY:
            pressHidKey(binding.keycode);
            break;
        case KeyAction::CTRL_WIN:
            pressHidKey(KEY_LEFT_CTRL);
            pressHidKey(KEY_LEFT_GUI);
            break;
        case KeyAction::FN:
            fn_pressed = true;
            break;
    }
}

void handleKeyRelease(uint8_t index) {
    const KeyBinding &binding = DEFAULT_KEY_BINDINGS[index];

    switch (binding.action) {
        case KeyAction::HID_KEY:
            releaseHidKey(binding.keycode);
            break;
        case KeyAction::CTRL_WIN:
            releaseHidKey(KEY_LEFT_GUI);
            releaseHidKey(KEY_LEFT_CTRL);
            break;
        case KeyAction::FN:
            fn_pressed = false;
            break;
    }
}

// ===== 开机画面 =====
void showSplash() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // Claude Logo 居中
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

// ===== 绘制单个按键状态 =====
void drawKeyBox(uint8_t index, bool pressed) {
    // 2 行 x 4 列网格布局
    // 每个按键框: 28x14 像素，间距 2px
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
        // 按下: 填充反转显示
        display.fillRect(x, y, box_w, box_h, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
    } else {
        // 释放: 空心框
        display.drawRect(x, y, box_w, box_h, SSD1306_WHITE);
        display.setTextColor(SSD1306_WHITE);
    }

    // 居中显示按键标签
    display.setTextSize(1);
    // 6x8 字体，单字符居中于 28x14 框
    const char *label = DEFAULT_KEY_BINDINGS[index].oled_label;
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

// ===== 渲染按键状态界面 =====
void renderKeyStatus() {
    if (keyboard_mode == KeyboardMode::USB && !usb_mounted) {
        renderUsbConnectionPrompt();
        return;
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // 顶栏: 标题 + 当前连接模式
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Vico ESP32-S3");

    display.setCursor(104, 0);
    if (keyboard_mode == KeyboardMode::USB) {
        display.print("USB");
    } else {
        display.print(bleKeyboard.isConnected() ? "BLE" : "...");
    }

    // 分隔线
    display.drawLine(0, 12, 127, 12, SSD1306_WHITE);

    // 绘制 8 个按键状态
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        drawKeyBox(i, key_state[i]);
    }

    // 底部: GPIO 映射提示
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 54);
    display.print("18 17 16 15 7 6 5 4");

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

    // 初始化 I2C + OLED
    Wire.begin(I2C_SDA, I2C_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[FAIL] SSD1306 init failed!");
        while (true) delay(1000);
    }
    Serial.println("SSD1306 init OK");

    // 初始化按键引脚
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        pinMode(DEFAULT_KEY_BINDINGS[i].pin, INPUT_PULLUP);
    }

    // 初始化 RGB 灯带
    rbgLedInit();

    startKeyboardInterface(keyboard_mode);

    // 跳过 Claude Code 开机图标，直接显示当前键盘状态。
    renderKeyStatus();
    display_dirty = false;
}

// ===== loop =====
void loop() {
    updateModeSwitch();
    unsigned long now = millis();
    updateUsbConnectionState(now);

    // 非阻塞更新 RGB 灯效
    rbgLedUpdate();

    // 扫描按键 + 去抖
    bool changed = false;
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        bool raw = (digitalRead(DEFAULT_KEY_BINDINGS[i].pin) == LOW);

        if (raw != key_state[i] && (now - last_change[i]) >= DEBOUNCE_MS) {
            key_state[i] = raw;
            last_change[i] = now;
            changed = true;
        }
    }

    // 通过当前选中的 USB 或 BLE HID 发送按键事件
    if (changed) {
        for (uint8_t i = 0; i < NUM_KEYS; i++) {
            if (key_state[i] && !last_reported[i]) {
                handleKeyPress(i);
            } else if (!key_state[i] && last_reported[i]) {
                handleKeyRelease(i);
            }
            last_reported[i] = key_state[i];
        }
        display_dirty = true;
    }

    // 未连接时定期刷新界面
    if (keyboard_mode == KeyboardMode::BLE && !bleKeyboard.isConnected()) {
        static unsigned long last_draw = 0;
        if (now - last_draw > 500) {
            last_draw = now;
            renderKeyStatus();
        }
        return;
    }

    // 按键状态变化时刷新 OLED
    if (display_dirty) {
        display_dirty = false;
        renderKeyStatus();
    }

    // Service digital-twin traffic only after key reports and OLED rendering.
    // update() sends at most one 63-byte Vendor HID report per loop pass.
    oledTwin.update(keyboard_mode == KeyboardMode::USB && usb_mounted);
}
