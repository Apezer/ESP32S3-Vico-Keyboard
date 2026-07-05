# ESP32-S3 BLE Mini Keyboard

基于 DFRobot FireBeetle 2 ESP32-S3 的 8 键蓝牙 HID 键盘，带 SSD1306 OLED 实时显示按键状态。

## 硬件

- **主控**: DFRobot FireBeetle 2 ESP32-S3
- **显示**: 0.96寸 OLED (SSD1306, 128x64, I2C)
- **按键**: 8 个轻触开关（下拉接地）

## 接线

| 功能 | GPIO |
|------|------|
| OLED SDA | 10 |
| OLED SCL | 9 |
| K1 (z) | 4 |
| K2 (x) | 5 |
| K3 (c) | 6 |
| K4 (v) | 7 |
| K5 (b) | 8 |
| K6 (n) | 14 |
| K7 (m) | 15 |
| K8 (a) | 16 |

## 功能

- BLE HID 键盘，设备名 `Vico Keyboard ESP32-S3`
- 8 个按键映射为 `z x c v b n m a`
- 10ms 按键去抖
- OLED 实时显示：标题栏、BLE 连接状态、2x4 按键网格（按下填充反转）、GPIO 映射
- 开机显示 Claude Logo

## 依赖库

- [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306) ^2.5.7
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) ^1.11.5
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) ^2.1.0
- [ESP32 NIMBLE Keyboard](https://github.com/Berg0162/ESP32-NIMBLE-Keyboard) v2.0.1（已内置在 `lib/ESP32-NIMBLE-Keyboard`）

## 构建

```bash
pio run
```

## NimBLE 说明

项目已从传统 ESP32 BLE Keyboard 库切换到 NimBLE 版本，以降低 RAM 和 Flash 占用。

当前使用 `NimBleKeyboard.h` 头文件，代码仍保持原来的 `BleKeyboard bleKeyboard;` 风格。

键盘库已复制到项目本地 `lib/ESP32-NIMBLE-Keyboard`，因此不需要在 `platformio.ini` 中从 GitHub 下载该库。

## 项目结构

```
├── platformio.ini          # PlatformIO 构建配置
├── src/
│   ├── main.cpp            # 主程序（BLE 键盘 + OLED 显示）
│   ├── font.h              # 字模 + Claude Logo 声明
│   └── font.c              # 字模 + Claude Logo 数据
└── lib/
    └── ESP32-NIMBLE-Keyboard # 本地内置的 NimBLE 键盘库
```
