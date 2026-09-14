#pragma once

/**
 * @file voice_capture.h
 * @brief INMP441 流式采集和语音传输协议。
 *
 * 音频采用 16 kHz、16 bit、单声道 PCM。USB 使用 32 KB 环形缓冲，
 * BLE 使用 128 KB 环形缓冲；录音期间一边采样一边发送。
 */

#include <Arduino.h>

class VoiceCapture {
public:
    static constexpr uint32_t SAMPLE_RATE = 16000;
    static constexpr uint8_t MAX_SECONDS = 15;
    static constexpr uint8_t PACKET_MAGIC = 0xA6;
    static constexpr uint8_t PROTOCOL_VERSION = 2;

    enum class State : uint8_t {
        IDLE,
        START_NOTICE,
        RECORDING,
        STOP_NOTICE,
        DRAINING,
        END_NOTICE,
        ERROR_NOTICE,
    };

    enum class PacketType : uint8_t {
        RECORDING_STARTED = 1,
        RECORDING_STOPPED = 2,
        AUDIO_CHUNK = 3,
        TRANSFER_END = 4,
        ERROR = 5,
    };

    enum class ErrorCode : uint8_t {
        MICROPHONE_UNAVAILABLE = 1,
        BUFFER_OVERFLOW = 2,
        TRANSPORT_UNAVAILABLE = 3,
    };

    /** @brief 准备录音模块；I2S 驱动延迟到第一次录音时安装。 */
    void begin();
    /**
     * @brief 旋钮按下时开始新的独占语音会话。
     * @param now 当前毫秒计数。
     * @param transportReady USB/BLE 接收端是否已就绪。
     * @param useBleBuffer BLE 模式传 true，启用完整 128 KB 缓冲。
     */
    bool start(uint32_t now, bool transportReady, bool useBleBuffer);
    /** @brief 旋钮松开时停止采样，并排空仍在缓冲区中的音频。 */
    bool stop(uint32_t now);
    /** @brief 非阻塞读取一块已经到达 I2S DMA 的数据。 */
    void update(uint32_t now);
    /** @brief 传输断开时终止会话，确保键盘不会永久停留在语音模式。 */
    void abort(ErrorCode error);

    /** @brief 构造下一份协议包；发送成功后必须调用 markPacketSent()。 */
    bool buildNextPacket(uint8_t *destination, size_t capacity, size_t &length) const;
    /** @brief 提交一次成功发送，并推进环形缓冲区读指针和协议状态。 */
    void markPacketSent(size_t packetLength);

    bool active() const { return state_ != State::IDLE; }
    bool recording() const {
        return state_ == State::START_NOTICE || state_ == State::RECORDING;
    }
    State state() const { return state_; }
    uint8_t levelPercent() const { return levelPercent_; }
    uint32_t recordedMilliseconds(uint32_t now) const;
    uint32_t capturedBytes() const { return producedBytes_; }
    uint32_t sentBytes() const { return sentBytes_; }
    bool takeDisplayDirty();

private:
    static constexpr uint8_t MIC_SD_PIN = 35;
    static constexpr uint8_t MIC_SCK_PIN = 36;
    static constexpr uint8_t MIC_WS_PIN = 37;
    static constexpr size_t USB_RING_BYTES = 32 * 1024;
    static constexpr size_t BLE_RING_BYTES = 128 * 1024;
    static constexpr size_t CHUNK_HEADER_BYTES = 9;

    State state_ = State::IDLE;
    ErrorCode error_ = ErrorCode::MICROPHONE_UNAVAILABLE;
    bool microphoneReady_ = false;
    // 只保留一块物理缓冲区；USB 模式限制其有效范围，避免维护两份队列。
    uint8_t ring_[BLE_RING_BYTES] = {};
    size_t activeRingBytes_ = USB_RING_BYTES;
    size_t readOffset_ = 0;
    size_t writeOffset_ = 0;
    size_t queuedBytes_ = 0;
    uint32_t producedBytes_ = 0;
    uint32_t sentBytes_ = 0;
    uint32_t runningCrc_ = 0xFFFFFFFFu;
    uint32_t finalCrc_ = 0;
    uint16_t sessionId_ = 0;
    uint32_t startedAt_ = 0;
    uint32_t stoppedAt_ = 0;
    uint32_t lastLevelAt_ = 0;
    uint8_t levelPercent_ = 0;
    bool displayDirty_ = false;

    bool ensureMicrophone();
    bool enqueueSample(int16_t sample);
    void finishCapture(uint32_t now);
    void resetStream();
    static uint32_t updateCrc32(uint32_t crc, const uint8_t *data, size_t length);
    static void writeUint16(uint8_t *destination, uint16_t value);
    static void writeUint32(uint8_t *destination, uint32_t value);
};
