# ESP32-S3 Vico Dual-Mode Keyboard

基于 DFRobot FireBeetle 2 ESP32-S3 的 8 键 USB/BLE 双模式 HID 键盘，带 SSD1306 OLED 实时显示按键状态。

## 硬件

- **主控**: DFRobot FireBeetle 2 ESP32-S3
- **显示**: 0.96寸 OLED (SSD1306, 128x64, I2C)
- **按键**: 8 个轻触开关（下拉接地）
- **模式开关**: SPDT 拨片开关，通过 GPIO35 在 BLE 与 USB 模式间选择
- **灯光**: 8 颗 WS2812B，数据引脚 GPIO8

## 接线

| 功能 | GPIO |
|------|------|
| OLED SDA | 10 |
| OLED SCL | 9 |
| K1（左方向键） | 18 |
| K2（下方向键） | 17 |
| K3（右方向键） | 16 |
| K4（Enter） | 15 |
| K5（Backspace） | 5 |
| K6（上方向键） | 6 |
| K7（Ctrl + Win） | 7 |
| K8（Fn，固件内部功能键） | 4 |
| WS2812B DIN | 8 |
| 模式选择（拨片公共端） | 35 |

模式拨片的公共端连接 GPIO35，另外两端分别连接 GND 和 3.3V：

- GPIO35 接 GND：BLE 模式
- GPIO35 接 3.3V：USB 模式

固件会持续监测拨片状态，并在电平稳定 50ms 后自动切换模式，无需重启。切换时会先释放旧模式的全部按键，再关闭旧接口并启动新接口，避免按键卡住。请使用 SPDT 拨片开关，避免将 3.3V 与 GND 直接短接。

## 功能

- USB/BLE 双模式 HID 键盘
- GPIO35 拨片开关运行时自动切换模式，无需重启
- USB 模式下使用 TinyUSB 检测主机枚举状态；未连接时 OLED 播放插头动画并显示 `PLUG IN USB`
- 插入 USB 后自动恢复按键界面，拔出后自动重新显示连接提醒
- BLE 设备名 `Vico Keyboard`
- USB 产品名 `Vico Keyboard`（VID/PID `3343:83CF`）
- USB 复合 HID：标准键盘 Report + Vendor Report ID 6
- OLED 数字孪生：按需回传 SSD1306 的 1024 字节真实帧，CRC32 校验
- OLED 运行时页面：品牌、Coding、电脑性能、时钟日期、键盘状态和自定义像素画
- 自定义像素画可通过 USB 或 BLE 分片传输，并在完整 CRC32 校验后显示
- 软件状态可通过 USB Vendor HID 或自定义 BLE GATT 服务发送
- `Fn + KEY6 / KEY7` 可向前或向后切换 OLED 页面
- OLED 顶栏显示当前 USB、BLE 或 BLE 未连接状态
- 默认按键映射：`左、下、右、Enter、Backspace、上、Ctrl+Win、Fn`
- 内置五套可编辑预设并保存到 NVS，断电后保留
- 按住 Fn 再按 KEY1～KEY5，可直接切换 P1～P5；Fn 和选择键不会发送给电脑
- USB Vendor HID 协议采用固定 5 字节按键定义、逐命令确认和 CRC32 原子提交
- KEY4 + KEY8 打开板载设置菜单，菜单操作不会发送 HID 按键
- 设置菜单可调整预设、OLED、RGB，并查看连接、按键测试和设备信息
- OLED 亮度/休眠/数字孪生与 RGB 灯效/亮度/速度/开关保存到 NVS
- OLED 默认页面和 Claude 自动覆盖设置保存到 NVS
- 10ms 按键去抖
- OLED 实时显示：标题栏、USB/BLE 模式、BLE 连接状态、2x4 按键网格（按下填充反转）、GPIO 映射

## 板载设置菜单

- `KEY4 + KEY8`：进入设置
- `KEY6`：向上
- `KEY2`：向下
- `KEY4`：确认
- `KEY5`：返回
- `KEY8`：退出设置

菜单包含 `PROFILE`、`OLED`、`RGB LIGHT`、`CONNECTION`、`KEY TEST`、`DEVICE INFO`、`FACTORY RESET` 和 `EXIT`。USB/BLE 模式仍由 GPIO35 硬件拨片决定，菜单只显示连接状态，不会覆盖拨片。

## 依赖库

- [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306) ^2.5.7
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) ^1.11.5
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) ^2.1.0
- [ESP32 NIMBLE Keyboard](https://github.com/Berg0162/ESP32-NIMBLE-Keyboard) v2.0.1（已内置在 `lib/ESP32-NIMBLE-Keyboard`）
- ESP32 Arduino Core 自带的 `USB` 和 `USBHIDKeyboard`

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
│   ├── main.cpp            # 主程序（USB/BLE 键盘 + OLED 显示）
│   ├── key_profiles.*      # 五套按键预设、CRC 与 NVS
│   ├── device_settings.*   # OLED/RGB 设置与 NVS
│   ├── rbg_led.*           # WS2812B 灯效
│   ├── font.h              # 字模 + Claude Logo 声明
│   └── font.c              # 字模 + Claude Logo 数据
└── lib/
    └── ESP32-NIMBLE-Keyboard # 本地内置的 NimBLE 键盘库
```
