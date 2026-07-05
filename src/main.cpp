/**
 * ESP32-S3 BLE Mini Keyboard
 *
 * 硬件: DFRobot FireBeetle2 ESP32-S3 + 0.96寸 OLED (SSD1306, I2C)
 *
 * 接线:
 *   OLED SDA -> GPIO 10
 *   OLED SCL -> GPIO 9
 *   OLED VCC -> 3.3V
 *   OLED GND -> GND
 *
 *   K1 -> GPIO 4   (z)
 *   K2 -> GPIO 5   (x)
 *   K3 -> GPIO 6   (c)
 *   K4 -> GPIO 7   (v)
 *   K5 -> GPIO 8   (b)
 *   K6 -> GPIO 14  (n)
 *   K7 -> GPIO 15  (m)
 *   K8 -> GPIO 16  (a)
 *
 * 依赖库 (platformio.ini 已配置):
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *   - ESP32 BLE Keyboard
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BleKeyboard.h>
#include "font.h"

// ===== 硬件配置 =====
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDR       0x3C

#define I2C_SDA         10
#define I2C_SCL         9

#define NUM_KEYS        8
#define DEBOUNCE_MS     10

// ===== 按键配置 =====
const uint8_t KEY_PINS[NUM_KEYS] = {4, 5, 6, 7, 8, 14, 15, 16};

const char KEY_CHARS[NUM_KEYS] = {'z', 'x', 'c', 'v', 'b', 'n', 'm', 'a'};

const char *KEY_LABELS[NUM_KEYS] = {
    "Z", "X", "C", "V",
    "B", "N", "M", "A",
};

// ===== 按键状态 =====
static bool key_state[NUM_KEYS] = {false};
static bool last_reported[NUM_KEYS] = {false};
static unsigned long last_change[NUM_KEYS] = {0};
static bool display_dirty = true;

// ===== 创建对象 =====
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
BleKeyboard bleKeyboard("Vico Keyboard ESP32-S3", "Apezer", 100);

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

    display.display();
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
    uint8_t text_x = x + (box_w - 6) / 2;
    uint8_t text_y = y + (box_h - 8) / 2;
    display.setCursor(text_x, text_y);
    display.print(KEY_LABELS[index]);
}

// ===== 渲染按键状态界面 =====
void renderKeyStatus() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // 顶栏: 标题 + 蓝牙状态
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Vico ESP32-S3");

    display.setCursor(104, 0);
    display.print(bleKeyboard.isConnected() ? "BLE" : "...");

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
    display.print("4 5 6 7 8 14 15 16");

    display.display();
}

// ===== setup =====
void setup() {
    Serial.begin(115200);
    Serial.println("=== Vico BLE Keyboard ESP32-S3 ===");

    // 初始化 BLE 键盘
    bleKeyboard.begin();

    // 初始化 I2C + OLED
    Wire.begin(I2C_SDA, I2C_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[FAIL] SSD1306 init failed!");
        while (true) delay(1000);
    }
    Serial.println("SSD1306 init OK");

    // 初始化按键引脚
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        pinMode(KEY_PINS[i], INPUT_PULLUP);
    }

    // 开机画面
    showSplash();
    delay(2000);
    display_dirty = true;
}

// ===== loop =====
void loop() {
    unsigned long now = millis();

    // 未连接时定期刷新界面
    if (!bleKeyboard.isConnected()) {
        static unsigned long last_draw = 0;
        if (now - last_draw > 500) {
            last_draw = now;
            renderKeyStatus();
        }
        return;
    }

    // 扫描按键 + 去抖
    bool changed = false;
    for (uint8_t i = 0; i < NUM_KEYS; i++) {
        bool raw = (digitalRead(KEY_PINS[i]) == LOW);

        if (raw != key_state[i] && (now - last_change[i]) >= DEBOUNCE_MS) {
            key_state[i] = raw;
            last_change[i] = now;
            changed = true;
        }
    }

    // 发送 BLE 按键事件
    if (changed) {
        for (uint8_t i = 0; i < NUM_KEYS; i++) {
            if (key_state[i] && !last_reported[i]) {
                bleKeyboard.press(KEY_CHARS[i]);
            } else if (!key_state[i] && last_reported[i]) {
                bleKeyboard.release(KEY_CHARS[i]);
            }
            last_reported[i] = key_state[i];
        }
        display_dirty = true;
    }

    // 按键状态变化时刷新 OLED
    if (display_dirty) {
        display_dirty = false;
        renderKeyStatus();
    }
}
