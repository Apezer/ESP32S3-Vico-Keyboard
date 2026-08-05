#pragma once

#include <Arduino.h>
#include "USBHIDVendor.h"

/**
 * Streams the exact SSD1306 framebuffer to the desktop application.
 *
 * Design goals:
 * - Never send data from the key scanning path.
 * - Send at most one HID report per update() call.
 * - Keep only the newest pending frame, so a slow host cannot build a backlog.
 * - Protect every complete frame with a sequence number and CRC32.
 */
class OledTwinTransport {
public:
    static constexpr size_t FRAME_BYTES = 128 * 64 / 8;

    /** Register the Vendor HID report before USB.begin() is called. */
    void begin();

    /** Drop the current host subscription when native USB is disconnected. */
    void resetSession();

    /**
     * Copy the latest framebuffer into the one-frame pending queue.
     * The source uses the native Adafruit SSD1306 page-major byte layout.
     */
    void captureFrame(const uint8_t *frame, size_t length);

    /** Process host commands and send at most one queued report. */
    void update(bool usbMounted);

private:
    static constexpr uint8_t REPORT_BYTES = 63;
    static constexpr uint8_t MAX_PAYLOAD_BYTES = 60;
    static constexpr uint8_t FRAME_CHUNK_BYTES = 56;
    static constexpr uint32_t REPORT_INTERVAL_MS = 1;

    enum class Command : uint8_t {
        HELLO = 0x01,
        DISPLAY_SUBSCRIBE = 0x02,
        HELLO_ACK = 0x81,
        FRAME_BEGIN = 0x82,
        FRAME_CHUNK = 0x83,
        FRAME_END = 0x84,
    };

    enum class TxState : uint8_t {
        IDLE,
        BEGIN,
        CHUNKS,
        END,
    };

    USBHIDVendor vendor_{REPORT_BYTES, false};
    bool started_ = false;
    bool subscribed_ = false;
    bool latestFramePending_ = false;
    bool controlPacketPending_ = false;
    TxState txState_ = TxState::IDLE;
    uint16_t nextFrameId_ = 1;
    uint16_t activeFrameId_ = 0;
    uint16_t txOffset_ = 0;
    uint32_t activeFrameCrc_ = 0;
    uint32_t lastReportAt_ = 0;
    uint8_t latestFrame_[FRAME_BYTES] = {};
    uint8_t txFrame_[FRAME_BYTES] = {};
    uint8_t controlPacket_[REPORT_BYTES] = {};

    void processHostCommand();
    void queueHelloAck();
    void startLatestFrame();
    bool sendControlPacket();
    bool sendFramePacket();
    bool sendPacket(Command command, const uint8_t *payload, uint8_t payloadLength);

    static bool validatePacket(const uint8_t *packet, size_t length);
    static uint8_t packetChecksum(const uint8_t *packet);
    static uint32_t calculateCrc32(const uint8_t *data, size_t length);
    static void writeUint16(uint8_t *destination, uint16_t value);
    static void writeUint32(uint8_t *destination, uint32_t value);
};
