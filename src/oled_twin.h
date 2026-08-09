#pragma once

/**
 * @file oled_twin.h
 * @brief 声明 USB Vendor HID 控制协议和 OLED 数字孪生传输状态机。
 *
 * 同一条 Vendor HID 通道同时承载握手、预设配置、运行时状态和 OLED 帧。
 * 控制包优先于帧分片，保证配置命令不会被持续画面传输阻塞。
 */

#include <Arduino.h>
#include "USBHIDVendor.h"
#include "key_profiles.h"
#include "oled_runtime.h"

/**
 * 将 SSD1306 的精确帧缓冲区传输到桌面应用。
 *
 * 设计目标：
 * - 不在按键扫描路径中发送数据。
 * - 每次 update() 调用最多发送一份 HID 报告。
 * - 只保留最新待发送帧，避免主机速度较慢时形成积压。
 * - 使用序列号和 CRC32 保护每个完整帧。
 */
class OledTwinTransport {
public:
    static constexpr size_t FRAME_BYTES = 128 * 64 / 8;

    /** 在调用 USB.begin() 前注册 Vendor HID 报告。 */
    void begin(KeyProfileManager *profiles, OledRuntime *runtime);

    /** 原生 USB 断开时清除当前主机订阅。 */
    void resetSession();

    /**
     * 将最新帧缓冲区复制到单帧待发送队列中。
     * 数据源使用 Adafruit SSD1306 原生的页优先字节布局。
     */
    void captureFrame(const uint8_t *frame, size_t length);

    /** 处理主机命令，并最多发送一份队列中的报告。 */
    void update(bool usbMounted);

    /** 命令更改活动预设或其绑定后返回一次 true。 */
    bool takeProfileChanged();

    /** 键盘本地切换预设后，将主动状态事件加入队列。 */
    void notifyActiveProfileChanged();
    /** @brief 键盘本地切换 OLED 页面后，将主动状态事件加入队列。 */
    void notifyRuntimeSettingsChanged(OledPage page, bool autoClaude);
    /** @brief 更新握手信息，并在USB端点启动后向软件推送最新电池状态。 */
    void notifyBatteryChanged(uint8_t percent, uint16_t millivolts);

private:
    // Vendor HID 报告固定为 63 字节：命令、长度、最多 60 字节负载、校验。
    static constexpr uint8_t REPORT_BYTES = 63;
    static constexpr uint8_t MAX_PAYLOAD_BYTES = 60;
    static constexpr uint8_t FRAME_CHUNK_BYTES = 56;
    static constexpr uint32_t REPORT_INTERVAL_MS = 1;

    /** @brief USB Vendor HID 命令与设备事件编号。 */
    enum class Command : uint8_t {
        HELLO = 0x01,
        DISPLAY_SUBSCRIBE = 0x02,
        PROFILE_BEGIN = 0x10,
        PROFILE_SET_KEY = 0x11,
        PROFILE_COMMIT = 0x12,
        PROFILE_SET_ACTIVE = 0x13,
        RUNTIME_UPDATE = 0x40,
        HELLO_ACK = 0x81,
        FRAME_BEGIN = 0x82,
        FRAME_CHUNK = 0x83,
        FRAME_END = 0x84,
        COMMAND_ACK = 0x90,
        PROFILE_ACTIVE_CHANGED = 0x91,
        RUNTIME_SETTINGS_CHANGED = 0x92,
        BATTERY_STATUS_CHANGED = 0x93,
    };

    /** @brief 一帧 1024 字节 OLED 数据的分片发送状态。 */
    enum class TxState : uint8_t {
        IDLE,
        BEGIN,
        CHUNKS,
        END,
    };

    // 协议依赖对象及连接/订阅状态。
    USBHIDVendor vendor_{REPORT_BYTES, false};
    KeyProfileManager *profiles_ = nullptr;
    OledRuntime *runtime_ = nullptr;
    bool started_ = false;
    bool subscribed_ = false;
    // 控制包和主动事件使用单槽合并队列，始终保留最新状态。
    bool latestFramePending_ = false;
    bool controlPacketPending_ = false;
    bool profileEventPending_ = false;
    bool runtimeSettingsEventPending_ = false;
    bool batteryEventPending_ = false;
    uint8_t runtimePage_ = 0;
    bool runtimeAutoClaude_ = true;
    uint8_t batteryPercent_ = 0;
    uint16_t batteryMillivolts_ = 0;
    bool profileChanged_ = false;
    // 当前 OLED 帧的分片发送状态。
    TxState txState_ = TxState::IDLE;
    uint16_t nextFrameId_ = 1;
    uint16_t activeFrameId_ = 0;
    uint16_t txOffset_ = 0;
    uint32_t activeFrameCrc_ = 0;
    uint32_t lastReportAt_ = 0;
    // latestFrame_ 可被新画面覆盖；txFrame_ 在一帧发送完成前保持不变。
    uint8_t latestFrame_[FRAME_BYTES] = {};
    uint8_t txFrame_[FRAME_BYTES] = {};
    uint8_t controlPacket_[REPORT_BYTES] = {};

    void processHostCommand();
    void queueHelloAck();
    void queueCommandAck(Command command, ProfileResult result, uint8_t profileIndex);
    void startLatestFrame();
    bool sendControlPacket();
    bool sendProfileChanged();
    bool sendRuntimeSettingsChanged();
    bool sendBatteryChanged();
    bool sendFramePacket();
    bool sendPacket(Command command, const uint8_t *payload, uint8_t payloadLength);

    static bool validatePacket(const uint8_t *packet, size_t length);
    static uint8_t packetChecksum(const uint8_t *packet);
    static uint32_t calculateCrc32(const uint8_t *data, size_t length);
    static void writeUint16(uint8_t *destination, uint16_t value);
    static void writeUint32(uint8_t *destination, uint32_t value);
};
