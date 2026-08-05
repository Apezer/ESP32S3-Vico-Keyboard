#include "key_profiles.h"

#include <Preferences.h>
#include <USBHIDKeyboard.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <utility>

namespace {
constexpr uint32_t STORAGE_MAGIC = 0x5649434F;  // "VICO"
constexpr uint8_t STORAGE_VERSION = 1;
constexpr char NVS_NAMESPACE[] = "vico_keys";
constexpr char NVS_BLOB_KEY[] = "profiles";

struct __attribute__((packed)) ProfileStorage {
    uint32_t magic;
    uint8_t version;
    uint8_t activeProfile;
    KeyBinding profiles[VICO_PROFILE_COUNT][VICO_KEY_COUNT];
    uint32_t crc;
};

constexpr KeyBinding keyboard(uint8_t keycode, uint8_t modifiers = MOD_NONE) {
    return {KeyActionType::KEYBOARD, modifiers, keycode, 0};
}

constexpr KeyBinding consumer(ConsumerAction action) {
    return {KeyActionType::CONSUMER, MOD_NONE, 0, static_cast<uint16_t>(action)};
}

constexpr KeyBinding fn() {
    return {KeyActionType::FN, MOD_NONE, 0, 0};
}

const KeyBinding FACTORY_PROFILES[VICO_PROFILE_COUNT][VICO_KEY_COUNT] = {
    {
        keyboard(KEY_LEFT_ARROW), keyboard(KEY_DOWN_ARROW),
        keyboard(KEY_RIGHT_ARROW), keyboard(KEY_RETURN),
        keyboard(KEY_BACKSPACE), keyboard(KEY_UP_ARROW),
        keyboard(0, MOD_CTRL | MOD_GUI), fn(),
    },
    {
        keyboard('c', MOD_CTRL), keyboard('v', MOD_CTRL),
        keyboard('z', MOD_CTRL), keyboard('z', MOD_CTRL | MOD_SHIFT),
        keyboard('s', MOD_CTRL), keyboard('f', MOD_CTRL),
        keyboard('a', MOD_CTRL), fn(),
    },
    {
        consumer(ConsumerAction::PREVIOUS_TRACK), consumer(ConsumerAction::PLAY_PAUSE),
        consumer(ConsumerAction::NEXT_TRACK), consumer(ConsumerAction::MUTE),
        consumer(ConsumerAction::VOLUME_DOWN), consumer(ConsumerAction::VOLUME_UP),
        consumer(ConsumerAction::STOP), fn(),
    },
    {
        keyboard(KEY_ESC), keyboard(KEY_TAB),
        keyboard('c', MOD_CTRL), keyboard('v', MOD_CTRL),
        keyboard('z', MOD_CTRL), keyboard('p', MOD_CTRL | MOD_SHIFT),
        keyboard('`', MOD_CTRL), fn(),
    },
    {
        keyboard('d', MOD_GUI), keyboard('e', MOD_GUI),
        keyboard('l', MOD_GUI), keyboard('s', MOD_GUI | MOD_SHIFT),
        keyboard(KEY_TAB, MOD_ALT), keyboard(KEY_ESC, MOD_CTRL | MOD_SHIFT),
        keyboard(KEY_DELETE), fn(),
    },
};

uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320 & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

const char *plainKeyLabel(uint8_t keycode) {
    switch (keycode) {
        case KEY_LEFT_ARROW: return "<";
        case KEY_DOWN_ARROW: return "DN";
        case KEY_RIGHT_ARROW: return ">";
        case KEY_UP_ARROW: return "UP";
        case KEY_RETURN: return "ENT";
        case KEY_BACKSPACE: return "BSP";
        case KEY_ESC: return "ESC";
        case KEY_TAB: return "TAB";
        case KEY_DELETE: return "DEL";
        default: return nullptr;
    }
}

char modifierLetter(uint8_t modifiers) {
    if (modifiers & MOD_CTRL) return 'C';
    if (modifiers & MOD_SHIFT) return 'S';
    if (modifiers & MOD_ALT) return 'A';
    if (modifiers & MOD_GUI) return 'W';
    return 0;
}
}

void KeyProfileManager::begin() {
    if (!loadFromNvs()) {
        loadFactoryDefaults();
        if (!saveToNvs()) Serial.println("[WARN] Could not save default key profiles");
    }
    Serial.printf("Key profiles ready; active profile P%u\n", activeProfile_ + 1);
}

const KeyBinding &KeyProfileManager::binding(uint8_t keyIndex) const {
    return profiles_[activeProfile_][keyIndex];
}

const KeyBinding &KeyProfileManager::binding(
    uint8_t profileIndex,
    uint8_t keyIndex
) const {
    return profiles_[profileIndex][keyIndex];
}

uint32_t KeyProfileManager::storedProfileCrc(uint8_t profileIndex) const {
    if (profileIndex >= VICO_PROFILE_COUNT) return 0;
    return profileCrc(profiles_[profileIndex]);
}

ProfileResult KeyProfileManager::beginUpdate(uint8_t profileIndex) {
    if (profileIndex >= VICO_PROFILE_COUNT) return ProfileResult::INVALID_SLOT;
    pendingProfile_ = profileIndex;
    pendingMask_ = 0;
    std::memset(pending_, 0, sizeof(pending_));
    return ProfileResult::OK;
}

ProfileResult KeyProfileManager::setPendingKey(
    uint8_t profileIndex,
    uint8_t keyIndex,
    const KeyBinding &nextBinding
) {
    if (profileIndex >= VICO_PROFILE_COUNT) return ProfileResult::INVALID_SLOT;
    if (keyIndex >= VICO_KEY_COUNT) return ProfileResult::INVALID_KEY;
    if (pendingProfile_ != profileIndex) return ProfileResult::NO_TRANSACTION;
    if (!isValidBinding(nextBinding)) return ProfileResult::INVALID_BINDING;

    pending_[keyIndex] = nextBinding;
    pendingMask_ |= static_cast<uint8_t>(1U << keyIndex);
    return ProfileResult::OK;
}

ProfileResult KeyProfileManager::commitUpdate(
    uint8_t profileIndex,
    uint32_t expectedCrc,
    bool activate
) {
    if (profileIndex >= VICO_PROFILE_COUNT) return ProfileResult::INVALID_SLOT;
    if (pendingProfile_ != profileIndex) return ProfileResult::NO_TRANSACTION;
    if (pendingMask_ != 0xFF) return ProfileResult::INCOMPLETE;
    if (profileCrc(pending_) != expectedCrc) return ProfileResult::CRC_MISMATCH;

    KeyBinding previous[VICO_KEY_COUNT];
    std::memcpy(previous, profiles_[profileIndex], sizeof(previous));
    const uint8_t previousActive = activeProfile_;

    std::memcpy(profiles_[profileIndex], pending_, sizeof(pending_));
    if (activate) activeProfile_ = profileIndex;

    if (!saveToNvs()) {
        std::memcpy(profiles_[profileIndex], previous, sizeof(previous));
        activeProfile_ = previousActive;
        return ProfileResult::STORAGE_ERROR;
    }

    pendingProfile_ = 0xFF;
    pendingMask_ = 0;
    return ProfileResult::OK;
}

ProfileResult KeyProfileManager::setActiveProfile(uint8_t profileIndex) {
    if (profileIndex >= VICO_PROFILE_COUNT) return ProfileResult::INVALID_SLOT;
    if (profileIndex == activeProfile_) return ProfileResult::OK;

    const uint8_t previous = activeProfile_;
    activeProfile_ = profileIndex;
    if (!saveToNvs()) {
        activeProfile_ = previous;
        return ProfileResult::STORAGE_ERROR;
    }
    return ProfileResult::OK;
}

bool KeyProfileManager::factoryReset() {
    loadFactoryDefaults();
    return saveToNvs();
}

bool KeyProfileManager::isValidBinding(const KeyBinding &candidate) {
    if ((candidate.modifiers & ~(MOD_CTRL | MOD_SHIFT | MOD_ALT | MOD_GUI)) != 0) {
        return false;
    }

    switch (candidate.action) {
        case KeyActionType::NONE:
            return candidate.modifiers == 0 && candidate.keycode == 0 && candidate.consumer == 0;
        case KeyActionType::KEYBOARD:
            return candidate.consumer == 0 && (candidate.keycode != 0 || candidate.modifiers != 0);
        case KeyActionType::CONSUMER:
            return candidate.modifiers == 0 && candidate.keycode == 0 &&
                candidate.consumer >= static_cast<uint16_t>(ConsumerAction::PREVIOUS_TRACK) &&
                candidate.consumer <= static_cast<uint16_t>(ConsumerAction::STOP);
        case KeyActionType::FN:
            return candidate.modifiers == 0 && candidate.keycode == 0 && candidate.consumer == 0;
    }
    return false;
}

uint32_t KeyProfileManager::profileCrc(const KeyBinding *bindings) {
    return crc32(reinterpret_cast<const uint8_t *>(bindings), sizeof(KeyBinding) * VICO_KEY_COUNT);
}

void KeyProfileManager::labelForBinding(
    const KeyBinding &binding,
    char *destination,
    size_t destinationSize
) {
    if (destinationSize == 0) return;
    destination[0] = '\0';

    if (binding.action == KeyActionType::FN) {
        std::snprintf(destination, destinationSize, "FN");
        return;
    }
    if (binding.action == KeyActionType::NONE) {
        std::snprintf(destination, destinationSize, "--");
        return;
    }
    if (binding.action == KeyActionType::CONSUMER) {
        switch (static_cast<ConsumerAction>(binding.consumer)) {
            case ConsumerAction::PREVIOUS_TRACK: std::snprintf(destination, destinationSize, "PRE"); return;
            case ConsumerAction::PLAY_PAUSE: std::snprintf(destination, destinationSize, "P/P"); return;
            case ConsumerAction::NEXT_TRACK: std::snprintf(destination, destinationSize, "NXT"); return;
            case ConsumerAction::MUTE: std::snprintf(destination, destinationSize, "MUTE"); return;
            case ConsumerAction::VOLUME_DOWN: std::snprintf(destination, destinationSize, "V-"); return;
            case ConsumerAction::VOLUME_UP: std::snprintf(destination, destinationSize, "V+"); return;
            case ConsumerAction::STOP: std::snprintf(destination, destinationSize, "STOP"); return;
            default: std::snprintf(destination, destinationSize, "MED"); return;
        }
    }

    if (binding.modifiers == 0) {
        if (const char *label = plainKeyLabel(binding.keycode)) {
            std::snprintf(destination, destinationSize, "%s", label);
        } else if (std::isprint(binding.keycode)) {
            std::snprintf(destination, destinationSize, "%c", std::toupper(binding.keycode));
        } else {
            std::snprintf(destination, destinationSize, "KEY");
        }
        return;
    }

    if (binding.keycode == 0 && binding.modifiers == (MOD_CTRL | MOD_GUI)) {
        std::snprintf(destination, destinationSize, "C+W");
        return;
    }

    char output[5] = {};
    size_t used = 0;
    for (const auto pair : {
        std::pair<uint8_t, char>{MOD_CTRL, 'C'},
        std::pair<uint8_t, char>{MOD_SHIFT, 'S'},
        std::pair<uint8_t, char>{MOD_ALT, 'A'},
        std::pair<uint8_t, char>{MOD_GUI, 'W'},
    }) {
        if ((binding.modifiers & pair.first) && used < sizeof(output) - 1) {
            output[used++] = pair.second;
        }
    }
    if (binding.keycode != 0 && used < sizeof(output) - 1) {
        const char *plain = plainKeyLabel(binding.keycode);
        output[used++] = plain ? plain[0] : static_cast<char>(std::toupper(binding.keycode));
    }
    output[used] = '\0';
    std::snprintf(destination, destinationSize, "%s", output);
}

void KeyProfileManager::loadFactoryDefaults() {
    std::memcpy(profiles_, FACTORY_PROFILES, sizeof(profiles_));
    activeProfile_ = 0;
    pendingProfile_ = 0xFF;
    pendingMask_ = 0;
}

bool KeyProfileManager::loadFromNvs() {
    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, true)) return false;

    ProfileStorage storage = {};
    const size_t storedLength = preferences.getBytesLength(NVS_BLOB_KEY);
    const size_t readLength = storedLength == sizeof(storage)
        ? preferences.getBytes(NVS_BLOB_KEY, &storage, sizeof(storage))
        : 0;
    preferences.end();

    if (readLength != sizeof(storage) || storage.magic != STORAGE_MAGIC ||
        storage.version != STORAGE_VERSION || storage.activeProfile >= VICO_PROFILE_COUNT) {
        return false;
    }

    const uint32_t expected = crc32(
        reinterpret_cast<const uint8_t *>(&storage.version),
        sizeof(storage.version) + sizeof(storage.activeProfile) + sizeof(storage.profiles)
    );
    if (storage.crc != expected) return false;

    for (uint8_t profile = 0; profile < VICO_PROFILE_COUNT; ++profile) {
        for (uint8_t key = 0; key < VICO_KEY_COUNT; ++key) {
            if (!isValidBinding(storage.profiles[profile][key])) return false;
        }
    }

    std::memcpy(profiles_, storage.profiles, sizeof(profiles_));
    activeProfile_ = storage.activeProfile;
    return true;
}

bool KeyProfileManager::saveToNvs() const {
    ProfileStorage storage = {};
    storage.magic = STORAGE_MAGIC;
    storage.version = STORAGE_VERSION;
    storage.activeProfile = activeProfile_;
    std::memcpy(storage.profiles, profiles_, sizeof(profiles_));
    storage.crc = crc32(
        reinterpret_cast<const uint8_t *>(&storage.version),
        sizeof(storage.version) + sizeof(storage.activeProfile) + sizeof(storage.profiles)
    );

    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, false)) return false;
    const size_t written = preferences.putBytes(NVS_BLOB_KEY, &storage, sizeof(storage));
    preferences.end();
    return written == sizeof(storage);
}
