/**
 * @file oled_runtime.cpp
 * @brief 实现 OLED 运行时二进制协议、BLE JSON 兼容层和页面覆盖规则。
 */

#include "oled_runtime.h"

#include <cstring>

namespace {
// =============================================================================
// BLE GATT 标识与无依赖 JSON 辅助函数
// =============================================================================
// 固件只需要三个短字符串字段，因此使用轻量解析器，避免为 32 字节状态消息
// 引入完整 JSON 库。该兼容路径主要服务于参考项目脚本和 BLE 调试工具。
constexpr char SERVICE_UUID[] = "7b6a0001-7c6e-4b3d-9f5f-7669636f0001";
constexpr char RX_UUID[] = "7b6a0002-7c6e-4b3d-9f5f-7669636f0002";
constexpr char TX_UUID[] = "7b6a0003-7c6e-4b3d-9f5f-7669636f0003";
constexpr uint32_t CLAUDE_DONE_OVERLAY_MS = 5000;

String jsonStringValue(const String &json, const char *key) {
    const String marker = String("\"") + key + "\"";
    int position = json.indexOf(marker);
    if (position < 0) return String();
    position = json.indexOf(':', position + marker.length());
    if (position < 0) return String();
    position = json.indexOf('"', position + 1);
    if (position < 0) return String();

    String result;
    bool escaping = false;
    for (int index = position + 1; index < static_cast<int>(json.length()); ++index) {
        const char value = json[index];
        if (escaping) {
            if (value == 'n') result += '\n';
            else if (value == 'r') result += '\r';
            else if (value == 't') result += '\t';
            else result += value;
            escaping = false;
        } else if (value == '\\') {
            escaping = true;
        } else if (value == '"') {
            break;
        } else {
            result += value;
        }
    }
    return result;
}

ClaudeState claudeStateFromText(const String &value) {
    if (value == "ready") return ClaudeState::READY;
    if (value == "working") return ClaudeState::WORKING;
    if (value == "tool") return ClaudeState::TOOL;
    if (value == "waiting") return ClaudeState::WAITING;
    if (value == "done") return ClaudeState::DONE;
    if (value == "error") return ClaudeState::ERROR_STATE;
    return ClaudeState::OFFLINE;
}

const char *claudeStateText(ClaudeState state) {
    static const char *const LABELS[] = {
        "offline", "ready", "working", "tool", "waiting", "done", "error",
    };
    const uint8_t index = static_cast<uint8_t>(state);
    return index < sizeof(LABELS) / sizeof(LABELS[0]) ? LABELS[index] : LABELS[0];
}

void copyAscii(char *destination, size_t capacity, const String &source) {
    if (capacity == 0) return;
    std::memset(destination, 0, capacity);
    const size_t length = source.length() < capacity - 1 ? source.length() : capacity - 1;
    std::memcpy(destination, source.c_str(), length);
}
}

// =============================================================================
// 页面设置与 32 字节运行时协议
// =============================================================================

void OledRuntime::configure(OledPage page, bool autoClaude) {
    if (static_cast<uint8_t>(page) > static_cast<uint8_t>(OledPage::CUSTOM)) {
        page = OledPage::BRAND;
    }
    state_.configuredPage = page;
    state_.autoClaude = autoClaude;
    dirty_ = true;
}

void OledRuntime::setPageSettings(OledPage page, bool autoClaude) {
    configure(page, autoClaude);
    settingsChanged_ = true;
    notifyPage();
}

bool OledRuntime::applyPacket(const uint8_t *data, size_t length) {
    if (!validPacket(data, length)) return false;

    // STATUS：一次更新 Claude 主状态、性能指标、电脑时间和当前工具。
    if (data[2] == PACKET_STATUS) {
        const auto nextClaudeState = data[3] <= static_cast<uint8_t>(ClaudeState::ERROR_STATE)
            ? static_cast<ClaudeState>(data[3])
            : ClaudeState::OFFLINE;
        if (nextClaudeState != state_.claudeState) {
            state_.claudeState = nextClaudeState;
            state_.claudeStateChangedAt = millis();
        }
        state_.cpu = data[4];
        state_.gpu = data[5];
        state_.memory = data[6];
        state_.temperature = data[7];
        state_.hour = data[8] <= 23 ? data[8] : 0;
        state_.minute = data[9] <= 59 ? data[9] : 0;
        state_.month = data[10] >= 1 && data[10] <= 12 ? data[10] : 1;
        state_.day = data[11] >= 1 && data[11] <= 31 ? data[11] : 1;
        state_.weekday = data[12] <= 6 ? data[12] : 0;
        state_.computerOnline = (data[13] & 0x01) != 0;
        const uint8_t toolLength = data[14] < 16 ? data[14] : 16;
        std::memset(state_.tool, 0, sizeof(state_.tool));
        std::memcpy(state_.tool, data + 15, toolLength);
        state_.receivedAt = millis();
        dirty_ = true;
        return true;
    }

    // CLAUDE_TEXT：补充 Coding 页长文本、活跃会话数和最近活动时间。
    if (data[2] == PACKET_CLAUDE_TEXT) {
        const uint8_t textLength = data[3] < 21 ? data[3] : 21;
        std::memset(state_.text, 0, sizeof(state_.text));
        std::memcpy(state_.text, data + 4, textLength);
        state_.activeSessions = data[25];
        state_.activityAgeSeconds = static_cast<uint16_t>(data[26] | (data[27] << 8));
        state_.activityAgeReceivedAt = millis();
        state_.receivedAt = millis();
        dirty_ = true;
        return true;
    }

    // SETTINGS：桌面软件选择默认页面或切换 Claude 自动覆盖。
    if (data[2] == PACKET_SETTINGS && data[3] <= static_cast<uint8_t>(OledPage::CUSTOM)) {
        const OledPage nextPage = static_cast<OledPage>(data[3]);
        const bool nextAutoClaude = data[4] != 0;
        if (nextPage == state_.configuredPage && nextAutoClaude == state_.autoClaude) return true;
        state_.configuredPage = nextPage;
        state_.autoClaude = nextAutoClaude;
        settingsChanged_ = true;
        dirty_ = true;
        return true;
    }

    // BITMAP_BEGIN：声明接下来固定为 1024 字节位图，并记录整帧 CRC32。
    if (data[2] == PACKET_BITMAP_BEGIN) {
        const uint16_t length = static_cast<uint16_t>(data[3] | (data[4] << 8));
        if (length != BITMAP_BYTES) return false;
        pendingBitmapCrc_ =
            static_cast<uint32_t>(data[5]) |
            (static_cast<uint32_t>(data[6]) << 8) |
            (static_cast<uint32_t>(data[7]) << 16) |
            (static_cast<uint32_t>(data[8]) << 24);
        pendingBitmapOffset_ = 0;
        bitmapTransferActive_ = true;
        return true;
    }

    // BITMAP_CHUNK：只接受严格连续的分片，任何错序都会取消本轮传输。
    if (data[2] == PACKET_BITMAP_CHUNK) {
        const uint16_t offset = static_cast<uint16_t>(data[3] | (data[4] << 8));
        if (!bitmapTransferActive_ || offset != pendingBitmapOffset_ || offset >= BITMAP_BYTES) {
            bitmapTransferActive_ = false;
            return false;
        }
        const size_t remaining = BITMAP_BYTES - offset;
        const size_t chunkLength = remaining < 26 ? remaining : 26;
        std::memcpy(pendingBitmap_ + offset, data + 5, chunkLength);
        pendingBitmapOffset_ += chunkLength;
        return true;
    }

    // BITMAP_COMMIT：长度和 CRC32 均通过后才替换正在显示的正式位图。
    if (data[2] == PACKET_BITMAP_COMMIT) {
        const uint32_t expectedCrc =
            static_cast<uint32_t>(data[3]) |
            (static_cast<uint32_t>(data[4]) << 8) |
            (static_cast<uint32_t>(data[5]) << 16) |
            (static_cast<uint32_t>(data[6]) << 24);
        const bool valid = bitmapTransferActive_ && pendingBitmapOffset_ == BITMAP_BYTES &&
            expectedCrc == pendingBitmapCrc_ &&
            calculateCrc32(pendingBitmap_, BITMAP_BYTES) == expectedCrc;
        bitmapTransferActive_ = false;
        if (!valid) return false;
        std::memcpy(customBitmap_, pendingBitmap_, BITMAP_BYTES);
        dirty_ = true;
        return true;
    }
    return false;
}

bool OledRuntime::applyJson(const char *data, size_t length) {
    if (data == nullptr || length == 0) return false;
    String json;
    json.reserve(length + 1);
    for (size_t index = 0; index < length; ++index) json += data[index];

    // state 是最小必需字段；tool/text 缺失时按空字符串处理。
    const String stateText = jsonStringValue(json, "state");
    if (stateText.length() == 0) return false;
    const ClaudeState nextState = claudeStateFromText(stateText);
    if (nextState != state_.claudeState) {
        state_.claudeState = nextState;
        state_.claudeStateChangedAt = millis();
    }
    copyAscii(state_.tool, sizeof(state_.tool), jsonStringValue(json, "tool"));
    copyAscii(state_.text, sizeof(state_.text), jsonStringValue(json, "text"));
    state_.activeSessions = nextState == ClaudeState::OFFLINE ? 0 : 1;
    state_.activityAgeSeconds = 0;
    state_.activityAgeReceivedAt = millis();
    state_.computerOnline = true;
    state_.receivedAt = millis();
    dirty_ = true;
    return true;
}

// =============================================================================
// 渲染快照、自动覆盖规则与变化标记
// =============================================================================

OledRuntimeSnapshot OledRuntime::snapshot() const {
    return state_;
}

OledPage OledRuntime::effectivePage(uint32_t now) const {
    // 自定义像素画是用户明确选择的画布，不允许 Coding 自动覆盖。
    if (state_.configuredPage == OledPage::CUSTOM) return OledPage::CUSTOM;

    const bool claudeActive =
        state_.claudeState == ClaudeState::READY ||
        state_.claudeState == ClaudeState::WORKING ||
        state_.claudeState == ClaudeState::TOOL ||
        state_.claudeState == ClaudeState::WAITING ||
        state_.claudeState == ClaudeState::ERROR_STATE;
    const bool recentlyDone = state_.claudeState == ClaudeState::DONE &&
        now - state_.claudeStateChangedAt < CLAUDE_DONE_OVERLAY_MS;

    if (state_.autoClaude && (claudeActive || recentlyDone)) return OledPage::CLAUDE;
    return state_.configuredPage;
}

const uint8_t *OledRuntime::customBitmap() const {
    return customBitmap_;
}

OledPage OledRuntime::cyclePage(int8_t direction) {
    int8_t page = static_cast<int8_t>(state_.configuredPage);
    page += direction >= 0 ? 1 : -1;
    if (page > static_cast<int8_t>(OledPage::CUSTOM)) page = 0;
    if (page < 0) page = static_cast<int8_t>(OledPage::CUSTOM);
    state_.configuredPage = static_cast<OledPage>(page);
    settingsChanged_ = true;
    dirty_ = true;
    notifyPage();
    return state_.configuredPage;
}

bool OledRuntime::takeDirty() {
    const bool dirty = dirty_;
    dirty_ = false;
    return dirty;
}

bool OledRuntime::takeSettingsChange(OledPage &page, bool &autoClaude) {
    if (!settingsChanged_) return false;
    settingsChanged_ = false;
    page = state_.configuredPage;
    autoClaude = state_.autoClaude;
    return true;
}

// =============================================================================
// BLE GATT 服务生命周期与写入回调
// =============================================================================

void OledRuntime::beginBle(NimBLEServer *server) {
    if (server == nullptr || rx_ != nullptr) return;
    NimBLEService *service = server->createService(SERVICE_UUID);
    rx_ = service->createCharacteristic(
        RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    tx_ = service->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    rx_->setCallbacks(this);
}

void OledRuntime::endBle() {
    rx_ = nullptr;
    tx_ = nullptr;
}

void OledRuntime::onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) {
    (void)connInfo;
    if (characteristic != rx_) return;
    const std::string value = characteristic->getValue();
    // 首个非空白字符为“{”时走 JSON 兼容路径，否则按 32 字节二进制包解析。
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && value[first] == '{') {
        if (applyJson(value.data(), value.size())) notifyJsonAck();
        return;
    }
    applyPacket(reinterpret_cast<const uint8_t *>(value.data()), value.size());
}

// =============================================================================
// 包校验、CRC32 与设备通知
// =============================================================================

bool OledRuntime::validPacket(const uint8_t *data, size_t length) {
    return data != nullptr && length == PACKET_BYTES &&
        data[0] == PACKET_MAGIC && data[1] == PACKET_VERSION &&
        data[PACKET_BYTES - 1] == checksum(data);
}

uint8_t OledRuntime::checksum(const uint8_t *data) {
    uint8_t value = 0;
    for (size_t index = 0; index < PACKET_BYTES - 1; ++index) value ^= data[index];
    return value;
}

uint32_t OledRuntime::calculateCrc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void OledRuntime::notifyPage() {
    if (tx_ == nullptr) return;
    uint8_t packet[PACKET_BYTES] = {};
    packet[0] = PACKET_MAGIC;
    packet[1] = PACKET_VERSION;
    packet[2] = PACKET_SETTINGS;
    packet[3] = static_cast<uint8_t>(state_.configuredPage);
    packet[4] = state_.autoClaude ? 1 : 0;
    packet[PACKET_BYTES - 1] = checksum(packet);
    tx_->setValue(packet, sizeof(packet));
    tx_->notify();
}

void OledRuntime::notifyJsonAck() {
    if (tx_ == nullptr) return;
    const String ack = String("{\"ok\":true,\"state\":\"") +
        claudeStateText(state_.claudeState) + "\",\"text\":\"" + state_.text + "\"}";
    tx_->setValue(ack.c_str());
    tx_->notify();
}
