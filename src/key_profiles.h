#pragma once

#include <Arduino.h>

constexpr uint8_t VICO_KEY_COUNT = 8;
constexpr uint8_t VICO_PROFILE_COUNT = 5;

enum class KeyActionType : uint8_t {
    NONE = 0,
    KEYBOARD = 1,
    CONSUMER = 2,
    FN = 3,
};

enum KeyModifier : uint8_t {
    MOD_NONE  = 0,
    MOD_CTRL  = 1 << 0,
    MOD_SHIFT = 1 << 1,
    MOD_ALT   = 1 << 2,
    MOD_GUI   = 1 << 3,
};

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

class KeyProfileManager {
public:
    /** 从 NVS 加载五套预设，失败时安装出厂默认值。 */
    void begin();

    const KeyBinding &binding(uint8_t keyIndex) const;
    const KeyBinding &binding(uint8_t profileIndex, uint8_t keyIndex) const;
    uint8_t activeProfile() const { return activeProfile_; }
    uint32_t storedProfileCrc(uint8_t profileIndex) const;

    ProfileResult beginUpdate(uint8_t profileIndex);
    ProfileResult setPendingKey(
        uint8_t profileIndex,
        uint8_t keyIndex,
        const KeyBinding &binding
    );
    ProfileResult commitUpdate(
        uint8_t profileIndex,
        uint32_t expectedCrc,
        bool activate
    );
    ProfileResult setActiveProfile(uint8_t profileIndex);
    bool factoryReset();

    static bool isValidBinding(const KeyBinding &binding);
    static uint32_t profileCrc(const KeyBinding *bindings);
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
