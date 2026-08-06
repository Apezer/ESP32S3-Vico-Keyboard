#pragma once

/**
 * @file oled_runtime.h
 * @brief 定义 OLED 运行时页面、Claude 状态和 USB/BLE 共用解析器。
 *
 * 桌面软件通过固定 32 字节二进制包更新运行数据；BLE 调试工具也可以写入
 * 兼容参考项目的紧凑 JSON。该模块只管理数据，不直接绘制 OLED。
 */

#include <Arduino.h>
#include <NimBLEDevice.h>

/** @brief 用户可以选择的 OLED 页面编号，数值属于持久化和通信协议。 */
enum class OledPage : uint8_t {
    BRAND = 0,
    CLAUDE = 1,
    SYSTEM = 2,
    CLOCK = 3,
    DEVICE = 4,
    CUSTOM = 5,
};

/** @brief Claude Code 工作状态，数值与桌面端运行时协议保持一致。 */
enum class ClaudeState : uint8_t {
    OFFLINE = 0,
    READY = 1,
    WORKING = 2,
    TOOL = 3,
    WAITING = 4,
    DONE = 5,
    ERROR_STATE = 6,
};

/**
 * @brief OLED 渲染层的一致性快照。
 *
 * 百分比字段使用 0xFF 表示电脑未提供该指标。字符串均预留结尾 NUL，
 * 可以直接交给 Adafruit_GFX 的 print() 使用。
 */
struct OledRuntimeSnapshot {
    // 页面选择和自动覆盖策略。
    OledPage configuredPage = OledPage::BRAND;
    bool autoClaude = true;

    // Claude Code 状态及电脑性能数据；0xFF 表示指标未知。
    ClaudeState claudeState = ClaudeState::OFFLINE;
    uint8_t cpu = 0xFF;
    uint8_t gpu = 0xFF;
    uint8_t memory = 0xFF;
    uint8_t temperature = 0xFF;

    // 由桌面软件同步的本地时间。
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t month = 1;
    uint8_t day = 1;
    uint8_t weekday = 0;
    bool computerOnline = false;

    // Coding 页面会话元数据和短 ASCII 文本。
    uint8_t activeSessions = 0;
    uint16_t activityAgeSeconds = 0;
    uint32_t activityAgeReceivedAt = 0;
    char tool[17] = {};
    char text[22] = {};

    // millis() 时间戳，用于数据新鲜度和 DONE 临时覆盖判断。
    uint32_t receivedAt = 0;
    uint32_t claudeStateChangedAt = 0;
};

/**
 * @brief 管理 OLED 运行时数据，并提供 USB 与 BLE 共用的数据包解析。
 *
 * 对外采用“写入状态 + 读取快照 + 消费 dirty 标记”的模式，避免 BLE 回调
 * 直接操作 I2C/OLED，从而保持线程边界和按键扫描路径清晰。
 */
class OledRuntime : public NimBLECharacteristicCallbacks {
public:
    static constexpr size_t PACKET_BYTES = 32;

    /** @brief 从持久化设置初始化页面，不触发设置回传。 */
    void configure(OledPage page, bool autoClaude);
    /** @brief 修改页面并标记需要同步回软件和 NVS。 */
    void setPageSettings(OledPage page, bool autoClaude);
    /**
     * @brief 校验并解析一份 32 字节运行时协议包。
     * @param data 指向完整数据包首字节。
     * @param length 数据长度，必须等于 PACKET_BYTES。
     * @return 包格式和内容均有效时返回 true。
     */
    bool applyPacket(const uint8_t *data, size_t length);
    /**
     * @brief 解析参考 GATT 项目的 state/tool/text JSON。
     * @param data UTF-8 JSON 字节；显示层只保留适合 OLED 的短文本。
     * @param length JSON 字节数。
     * @return 至少包含有效 state 字段时返回 true。
     */
    bool applyJson(const char *data, size_t length);
    /** @brief 返回当前运行数据的值拷贝，供渲染层安全使用。 */
    OledRuntimeSnapshot snapshot() const;
    /** @brief 根据自动覆盖规则计算此刻真正应该显示的页面。 */
    OledPage effectivePage(uint32_t now) const;
    const uint8_t *customBitmap() const;

    /** @brief 循环切换用户配置页；direction 非负表示下一页。 */
    OledPage cyclePage(int8_t direction);
    /** @brief 消费一次待重绘标记。 */
    bool takeDirty();
    /** @brief 消费一次本地页面设置变化。 */
    bool takeSettingsChange(OledPage &page, bool &autoClaude);

    /**
     * @brief 在蓝牙 HID 服务启动期间注册自定义状态服务。
     * @param server 已创建但尚未启动全部服务的 NimBLE 服务器。
     */
    void beginBle(NimBLEServer *server);
    /** @brief BLE 协议栈关闭后清理失效的 characteristic 指针。 */
    void endBle();

protected:
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override;

private:
    // 固定 32 字节二进制协议常量；最后一字节始终为异或校验值。
    static constexpr uint8_t PACKET_MAGIC = 0x56;
    static constexpr uint8_t PACKET_VERSION = 1;
    static constexpr uint8_t PACKET_STATUS = 1;
    static constexpr uint8_t PACKET_SETTINGS = 2;
    static constexpr uint8_t PACKET_BITMAP_BEGIN = 3;
    static constexpr uint8_t PACKET_BITMAP_CHUNK = 4;
    static constexpr uint8_t PACKET_BITMAP_COMMIT = 5;
    static constexpr uint8_t PACKET_CLAUDE_TEXT = 6;
    static constexpr size_t BITMAP_BYTES = 128 * 64 / 8;

    // 当前正式运行状态和主循环使用的单次变化标记。
    OledRuntimeSnapshot state_ = {};
    bool dirty_ = true;
    bool settingsChanged_ = false;
    // BLE characteristic 仅在 BLE 协议栈存活期间有效。
    NimBLECharacteristic *rx_ = nullptr;
    NimBLECharacteristic *tx_ = nullptr;
    // 双缓冲保证传输失败时仍继续显示上一张完整自定义位图。
    uint8_t customBitmap_[BITMAP_BYTES] = {};
    uint8_t pendingBitmap_[BITMAP_BYTES] = {};
    uint16_t pendingBitmapOffset_ = 0;
    uint32_t pendingBitmapCrc_ = 0;
    bool bitmapTransferActive_ = false;

    static bool validPacket(const uint8_t *data, size_t length);
    static uint8_t checksum(const uint8_t *data);
    static uint32_t calculateCrc32(const uint8_t *data, size_t length);
    void notifyPage();
    void notifyJsonAck();
};
