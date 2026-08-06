#pragma once

/**
 * @file key_profiles.h
 * @brief 定义五套按键预设、按键绑定线上格式和原子更新接口。
 *
 * 桌面软件与固件共同依赖 KeyBinding 的 5 字节布局。任何字段变更都需要同步
 * PROFILE_SET_KEY 协议、CRC 计算和桌面端编码器。
 */

#include <Arduino.h>

constexpr uint8_t VICO_KEY_COUNT = 8;
constexpr uint8_t VICO_PROFILE_COUNT = 5;

/** @brief 一个按键可执行的动作类别。 */
enum class KeyActionType : uint8_t {
    NONE = 0,
    KEYBOARD = 1,
    CONSUMER = 2,
    FN = 3,
};

/** @brief 可按位组合的 HID 修饰键掩码。 */
enum KeyModifier : uint8_t {
    MOD_NONE  = 0,
    MOD_CTRL  = 1 << 0,
    MOD_SHIFT = 1 << 1,
    MOD_ALT   = 1 << 2,
    MOD_GUI   = 1 << 3,
};

/** @brief 固件支持的 USB/BLE Consumer Control 动作。 */
enum class ConsumerAction : uint16_t {
    NONE = 0,
    PREVIOUS_TRACK = 1,
    PLAY_PAUSE = 2,
    NEXT_TRACK = 3,
    MUTE = 4,
    VOLUME_DOWN = 5,
    VOLUME_UP = 6,
    STOP = 7,
};

/**
 * 紧凑且适合传输的按键定义。
 *
 * 该字节布局同时也是 PROFILE_SET_KEY 使用的线上格式：
 * 动作、修饰键、键码、消费者控制低字节、消费者控制高字节。
 */
struct __attribute__((packed)) KeyBinding {
    KeyActionType action;
    uint8_t modifiers;
    uint8_t keycode;
    uint16_t consumer;
};

static_assert(sizeof(KeyBinding) == 5, "KeyBinding wire layout must remain stable");

/** @brief 预设命令的稳定返回码，同时用于 USB Vendor HID ACK。 */
enum class ProfileResult : uint8_t {
    OK = 0,
    INVALID_SLOT = 1,
    INVALID_KEY = 2,
    INVALID_BINDING = 3,
    NO_TRANSACTION = 4,
    INCOMPLETE = 5,
    CRC_MISMATCH = 6,
    STORAGE_ERROR = 7,
    BUSY = 8,
};

/**
 * @brief 管理五套按键预设及其 NVS 持久化。
 *
 * 软件更新预设时先 beginUpdate()，再逐键写入临时缓冲区，最后通过
 * commitUpdate() 校验完整性并一次性替换正式预设，避免断线留下半套配置。
 */
class KeyProfileManager {
public:
    /** 从 NVS 加载五套预设，失败时安装出厂默认值。 */
    void begin();

    /** @brief 获取活动预设中的指定按键绑定。 */
    const KeyBinding &binding(uint8_t keyIndex) const;
    /** @brief 获取指定预设和按键的绑定。 */
    const KeyBinding &binding(uint8_t profileIndex, uint8_t keyIndex) const;
    uint8_t activeProfile() const { return activeProfile_; }
    uint32_t storedProfileCrc(uint8_t profileIndex) const;

    /** @brief 开始一笔预设原子更新事务。 */
    ProfileResult beginUpdate(uint8_t profileIndex);
    /** @brief 在当前事务的临时缓冲区写入一个按键，但不修改正式预设。 */
    ProfileResult setPendingKey(
        uint8_t profileIndex,
        uint8_t keyIndex,
        const KeyBinding &binding
    );
    /** @brief CRC 校验通过后提交完整预设，并可选择立即激活。 */
    ProfileResult commitUpdate(
        uint8_t profileIndex,
        uint32_t expectedCrc,
        bool activate
    );
    /** @brief 激活并持久化指定预设槽位。 */
    ProfileResult setActiveProfile(uint8_t profileIndex);
    /** @brief 恢复五套出厂预设并保存。 */
    bool factoryReset();

    static bool isValidBinding(const KeyBinding &binding);
    static uint32_t profileCrc(const KeyBinding *bindings);
    /** @brief 生成适合 6×8 OLED 字体显示的最多四字符标签。 */
    static void labelForBinding(
        const KeyBinding &binding,
        char *destination,
        size_t destinationSize
    );

private:
    KeyBinding profiles_[VICO_PROFILE_COUNT][VICO_KEY_COUNT] = {};
    KeyBinding pending_[VICO_KEY_COUNT] = {};
    uint8_t activeProfile_ = 0;
    uint8_t pendingProfile_ = 0xFF;
    uint8_t pendingMask_ = 0;

    void loadFactoryDefaults();
    bool loadFromNvs();
    bool saveToNvs() const;
};
