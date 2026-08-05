#include "oled_twin.h"

#include <cstring>
#include "tusb.h"

namespace {
constexpr uint8_t PROTOCOL_VERSION = 2;
constexpr uint8_t DISPLAY_WIDTH = 128;
constexpr uint8_t DISPLAY_HEIGHT = 64;
constexpr uint8_t SSD1306_PAGE_LAYOUT = 1;
constexpr uint8_t FIRMWARE_VERSION_MAJOR = 0;
constexpr uint8_t FIRMWARE_VERSION_MINOR = 5;
constexpr uint8_t FIRMWARE_VERSION_PATCH = 0;
}

void OledTwinTransport::begin(KeyProfileManager *profiles) {
    if (started_) return;

    profiles_ = profiles;

    // Four incoming packets are sufficient for commands, but a larger queue also
    // tolerates short host bursts without ever blocking the key scanning loop.
    vendor_.setRxBufferSize(512);
    vendor_.begin();
    started_ = true;
}

bool OledTwinTransport::takeProfileChanged() {
    const bool changed = profileChanged_;
    profileChanged_ = false;
    return changed;
}

void OledTwinTransport::notifyActiveProfileChanged() {
    // Keep one coalesced event. If the user switches several times before the
    // endpoint becomes ready, the host only needs the final active slot.
    if (profiles_ != nullptr) profileEventPending_ = true;
}

void OledTwinTransport::resetSession() {
    subscribed_ = false;
    controlPacketPending_ = false;
    profileEventPending_ = false;
    txState_ = TxState::IDLE;
    txOffset_ = 0;

    // Discard commands that belonged to the host which just disconnected.
    while (started_ && vendor_.available() > 0) vendor_.read();
}

void OledTwinTransport::captureFrame(const uint8_t *frame, size_t length) {
    if (frame == nullptr || length != FRAME_BYTES) return;

    std::memcpy(latestFrame_, frame, FRAME_BYTES);
    latestFramePending_ = true;
}

void OledTwinTransport::update(bool usbMounted) {
    if (!started_ || !usbMounted) return;

    processHostCommand();

    // TinyUSB exposes one shared HID endpoint. Waiting for it to become ready
    // avoids a blocking retry and leaves keyboard reports with first priority.
    if (!tud_hid_ready()) return;
    if (millis() - lastReportAt_ < REPORT_INTERVAL_MS) return;

    if (controlPacketPending_) {
        sendControlPacket();
        return;
    }

    // Device-originated state changes are more important than display frames
    // and are sent outside the key scanning path.
    if (profileEventPending_) {
        if (sendProfileChanged()) profileEventPending_ = false;
        return;
    }

    if (!subscribed_) return;
    if (txState_ == TxState::IDLE && latestFramePending_) startLatestFrame();
    if (txState_ != TxState::IDLE) sendFramePacket();
}

void OledTwinTransport::processHostCommand() {
    if (vendor_.available() < REPORT_BYTES) return;

    uint8_t packet[REPORT_BYTES] = {};
    if (vendor_.read(packet, REPORT_BYTES) != REPORT_BYTES) return;
    if (!validatePacket(packet, REPORT_BYTES)) return;

    const auto command = static_cast<Command>(packet[0]);
    const uint8_t payloadLength = packet[1];

    switch (command) {
        case Command::HELLO:
            queueHelloAck();
            break;

        case Command::DISPLAY_SUBSCRIBE:
            if (payloadLength >= 1) {
                subscribed_ = packet[2] != 0;
                txState_ = TxState::IDLE;
                txOffset_ = 0;
                if (subscribed_) latestFramePending_ = true;
            }
            break;

        case Command::PROFILE_BEGIN: {
            ProfileResult result = ProfileResult::INVALID_BINDING;
            const uint8_t profileIndex = payloadLength >= 2 ? packet[3] : 0xFF;
            if (profiles_ != nullptr && payloadLength >= 2 && packet[2] == 1) {
                result = profiles_->beginUpdate(profileIndex);
            }
            queueCommandAck(command, result, profileIndex);
            break;
        }

        case Command::PROFILE_SET_KEY: {
            const uint8_t profileIndex = payloadLength >= 1 ? packet[2] : 0xFF;
            const uint8_t keyIndex = payloadLength >= 2 ? packet[3] : 0xFF;
            ProfileResult result = ProfileResult::INVALID_BINDING;
            if (profiles_ != nullptr && payloadLength >= 7) {
                const KeyBinding binding = {
                    static_cast<KeyActionType>(packet[4]),
                    packet[5],
                    packet[6],
                    static_cast<uint16_t>(packet[7] | (packet[8] << 8)),
                };
                result = profiles_->setPendingKey(profileIndex, keyIndex, binding);
            }
            queueCommandAck(command, result, profileIndex);
            break;
        }

        case Command::PROFILE_COMMIT: {
            const uint8_t profileIndex = payloadLength >= 1 ? packet[2] : 0xFF;
            ProfileResult result = ProfileResult::INVALID_BINDING;
            if (profiles_ != nullptr && payloadLength >= 6) {
                const bool activate = packet[3] != 0;
                const uint32_t expectedCrc =
                    static_cast<uint32_t>(packet[4]) |
                    (static_cast<uint32_t>(packet[5]) << 8) |
                    (static_cast<uint32_t>(packet[6]) << 16) |
                    (static_cast<uint32_t>(packet[7]) << 24);
                result = profiles_->commitUpdate(profileIndex, expectedCrc, activate);
                if (result == ProfileResult::OK) profileChanged_ = true;
            }
            queueCommandAck(command, result, profileIndex);
            break;
        }

        case Command::PROFILE_SET_ACTIVE: {
            const uint8_t profileIndex = payloadLength >= 1 ? packet[2] : 0xFF;
            const ProfileResult result = profiles_ != nullptr && payloadLength >= 1
                ? profiles_->setActiveProfile(profileIndex)
                : ProfileResult::INVALID_SLOT;
            if (result == ProfileResult::OK) profileChanged_ = true;
            queueCommandAck(command, result, profileIndex);
            break;
        }

        default:
            // Custom OLED upload commands are versioned separately.
            break;
    }
}

void OledTwinTransport::queueHelloAck() {
    uint8_t payload[13 + VICO_PROFILE_COUNT * 4] = {
        PROTOCOL_VERSION,
        DISPLAY_WIDTH,
        DISPLAY_HEIGHT,
        SSD1306_PAGE_LAYOUT,
        FIRMWARE_VERSION_MAJOR,
        FIRMWARE_VERSION_MINOR,
        FIRMWARE_VERSION_PATCH,
        'V', 'I', 'C', 'O',
        VICO_PROFILE_COUNT,
        static_cast<uint8_t>(profiles_ != nullptr ? profiles_->activeProfile() : 0),
    };

    for (uint8_t profile = 0; profile < VICO_PROFILE_COUNT; ++profile) {
        const uint32_t crc = profiles_ != nullptr
            ? profiles_->storedProfileCrc(profile)
            : 0;
        writeUint32(payload + 13 + profile * 4, crc);
    }

    std::memset(controlPacket_, 0, REPORT_BYTES);
    controlPacket_[0] = static_cast<uint8_t>(Command::HELLO_ACK);
    controlPacket_[1] = sizeof(payload);
    std::memcpy(controlPacket_ + 2, payload, sizeof(payload));
    controlPacket_[REPORT_BYTES - 1] = packetChecksum(controlPacket_);
    controlPacketPending_ = true;
}

void OledTwinTransport::queueCommandAck(
    Command command,
    ProfileResult result,
    uint8_t profileIndex
) {
    const uint8_t payload[] = {
        static_cast<uint8_t>(command),
        static_cast<uint8_t>(result),
        profileIndex,
        static_cast<uint8_t>(profiles_ != nullptr ? profiles_->activeProfile() : 0),
    };

    std::memset(controlPacket_, 0, REPORT_BYTES);
    controlPacket_[0] = static_cast<uint8_t>(Command::COMMAND_ACK);
    controlPacket_[1] = sizeof(payload);
    std::memcpy(controlPacket_ + 2, payload, sizeof(payload));
    controlPacket_[REPORT_BYTES - 1] = packetChecksum(controlPacket_);
    controlPacketPending_ = true;
}

void OledTwinTransport::startLatestFrame() {
    std::memcpy(txFrame_, latestFrame_, FRAME_BYTES);
    latestFramePending_ = false;
    activeFrameId_ = nextFrameId_++;
    activeFrameCrc_ = calculateCrc32(txFrame_, FRAME_BYTES);
    txOffset_ = 0;
    txState_ = TxState::BEGIN;
}

bool OledTwinTransport::sendControlPacket() {
    if (vendor_.write(controlPacket_, REPORT_BYTES) != REPORT_BYTES) return false;

    controlPacketPending_ = false;
    lastReportAt_ = millis();
    return true;
}

bool OledTwinTransport::sendProfileChanged() {
    const uint8_t payload[] = {
        static_cast<uint8_t>(profiles_ != nullptr ? profiles_->activeProfile() : 0),
    };
    return sendPacket(Command::PROFILE_ACTIVE_CHANGED, payload, sizeof(payload));
}

bool OledTwinTransport::sendFramePacket() {
    uint8_t payload[MAX_PAYLOAD_BYTES] = {};
    uint8_t payloadLength = 0;
    Command command = Command::FRAME_BEGIN;

    switch (txState_) {
        case TxState::BEGIN:
            command = Command::FRAME_BEGIN;
            writeUint16(payload, activeFrameId_);
            writeUint16(payload + 2, FRAME_BYTES);
            writeUint32(payload + 4, activeFrameCrc_);
            payload[8] = SSD1306_PAGE_LAYOUT;
            payloadLength = 9;
            break;

        case TxState::CHUNKS: {
            command = Command::FRAME_CHUNK;
            const uint16_t remaining = FRAME_BYTES - txOffset_;
            const uint8_t chunkLength = remaining > FRAME_CHUNK_BYTES
                ? FRAME_CHUNK_BYTES
                : static_cast<uint8_t>(remaining);
            writeUint16(payload, activeFrameId_);
            writeUint16(payload + 2, txOffset_);
            std::memcpy(payload + 4, txFrame_ + txOffset_, chunkLength);
            payloadLength = 4 + chunkLength;
            break;
        }

        case TxState::END:
            command = Command::FRAME_END;
            writeUint16(payload, activeFrameId_);
            writeUint32(payload + 2, activeFrameCrc_);
            payloadLength = 6;
            break;

        case TxState::IDLE:
            return false;
    }

    if (!sendPacket(command, payload, payloadLength)) return false;

    if (txState_ == TxState::BEGIN) {
        txState_ = TxState::CHUNKS;
    } else if (txState_ == TxState::CHUNKS) {
        txOffset_ += payloadLength - 4;
        if (txOffset_ >= FRAME_BYTES) txState_ = TxState::END;
    } else if (txState_ == TxState::END) {
        txState_ = TxState::IDLE;
    }
    return true;
}

bool OledTwinTransport::sendPacket(
    Command command,
    const uint8_t *payload,
    uint8_t payloadLength
) {
    if (payloadLength > MAX_PAYLOAD_BYTES) return false;

    uint8_t packet[REPORT_BYTES] = {};
    packet[0] = static_cast<uint8_t>(command);
    packet[1] = payloadLength;
    if (payloadLength > 0) std::memcpy(packet + 2, payload, payloadLength);
    packet[REPORT_BYTES - 1] = packetChecksum(packet);

    if (vendor_.write(packet, REPORT_BYTES) != REPORT_BYTES) return false;
    lastReportAt_ = millis();
    return true;
}

bool OledTwinTransport::validatePacket(const uint8_t *packet, size_t length) {
    if (packet == nullptr || length != REPORT_BYTES) return false;
    if (packet[1] > MAX_PAYLOAD_BYTES) return false;
    return packet[REPORT_BYTES - 1] == packetChecksum(packet);
}

uint8_t OledTwinTransport::packetChecksum(const uint8_t *packet) {
    uint8_t checksum = 0;
    for (uint8_t index = 0; index < REPORT_BYTES - 1; ++index) {
        checksum ^= packet[index];
    }
    return checksum;
}

uint32_t OledTwinTransport::calculateCrc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void OledTwinTransport::writeUint16(uint8_t *destination, uint16_t value) {
    destination[0] = value & 0xFF;
    destination[1] = value >> 8;
}

void OledTwinTransport::writeUint32(uint8_t *destination, uint32_t value) {
    destination[0] = value & 0xFF;
    destination[1] = (value >> 8) & 0xFF;
    destination[2] = (value >> 16) & 0xFF;
    destination[3] = (value >> 24) & 0xFF;
}
