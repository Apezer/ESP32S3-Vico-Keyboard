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
- [ESP32 BLE Keyboard](https://github.com/T-vK/ESP32-BLE-Keyboard) ^0.3.2

## 构建

```bash
pio run
```

## 修改说明

### 库文件修改

修改了 `ESP32 BLE Keyboard` 库的 `BleKeyboard.cpp` 第 130 行，将 BLE 安全认证模式从 `ESP_LE_AUTH_REQ_SC_MITM_BOND` 改为 `ESP_LE_AUTH_BOND`，以解决蓝牙反复断开的问题。

```cpp
// 修改前
pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);

// 修改后
pSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND);
```

此修改位于 `.pio/libdeps/` 目录下的本地库文件，重新安装库（`pio lib update`）后需要再次手动修改。

## 项目结构

```
├── platformio.ini          # PlatformIO 构建配置
├── src/
│   ├── main.cpp            # 主程序（BLE 键盘 + OLED 显示）
│   ├── font.h              # 字模 + Claude Logo 声明
│   └── font.c              # 字模 + Claude Logo 数据
└── lib/                    # 第三方库目录
```
