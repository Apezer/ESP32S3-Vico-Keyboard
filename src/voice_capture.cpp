/**
 * @file voice_capture.cpp
 * @brief 实现 INMP441 非阻塞采集、固定环形缓冲和流式分片协议。
 */

#include "voice_capture.h"

#include <driver/i2s.h>
#include <cstring>

namespace {
constexpr i2s_port_t MIC_I2S_PORT = I2S_NUM_0;
constexpr size_t READ_SAMPLES = 256;
int32_t rawSamples[READ_SAMPLES] = {};
}

void VoiceCapture::begin() {
    // 延迟安装 I2S，确保 BLE/USB HID 在开机阶段先完成初始化。
    microphoneReady_ = false;
    state_ = State::IDLE;
}

bool VoiceCapture::ensureMicrophone() {
    if (microphoneReady_) return true;

    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
    config.sample_rate = SAMPLE_RATE;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 6;
    config.dma_buf_len = READ_SAMPLES;
    config.use_apll = false;
    config.tx_desc_auto_clear = false;
    config.fixed_mclk = 0;

    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = MIC_SCK_PIN;
    pins.ws_io_num = MIC_WS_PIN;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = MIC_SD_PIN;

    if (i2s_driver_install(MIC_I2S_PORT, &config, 0, nullptr) != ESP_OK) return false;
    if (i2s_set_pin(MIC_I2S_PORT, &pins) != ESP_OK) {
        i2s_driver_uninstall(MIC_I2S_PORT);
        return false;
    }

    i2s_zero_dma_buffer(MIC_I2S_PORT);
    microphoneReady_ = true;
    return true;
}

bool VoiceCapture::start(uint32_t now, bool transportReady) {
    if (state_ != State::IDLE) return false;
    if (!transportReady) {
        abort(ErrorCode::TRANSPORT_UNAVAILABLE);
        return false;
    }
    if (!ensureMicrophone()) {
        abort(ErrorCode::MICROPHONE_UNAVAILABLE);
        return false;
    }

    resetStream();
    startedAt_ = now;
    ++sessionId_;
    if (sessionId_ == 0) ++sessionId_;

    // 丢弃空闲期间残留在 DMA 中的旧采样，且始终使用零等待避免卡住主循环。
    size_t discardedBytes = 0;
    for (uint8_t index = 0; index < 8; ++index) {
        if (i2s_read(MIC_I2S_PORT, rawSamples, sizeof(rawSamples), &discardedBytes, 0) != ESP_OK ||
            discardedBytes == 0) break;
    }

    state_ = State::START_NOTICE;
    displayDirty_ = true;
    return true;
}

bool VoiceCapture::stop(uint32_t now) {
    if (!recording()) return false;
    finishCapture(now);
    return true;
}

void VoiceCapture::finishCapture(uint32_t now) {
    stoppedAt_ = now;
    finalCrc_ = ~runningCrc_;
    levelPercent_ = 0;
    state_ = State::STOP_NOTICE;
    displayDirty_ = true;
}

void VoiceCapture::update(uint32_t now) {
    if (state_ == State::ERROR_NOTICE) {
        // 无接收端时错误包无法发出；短暂显示错误后自动恢复键盘功能。
        if (stoppedAt_ != 0 && now - stoppedAt_ >= 1800) {
            state_ = State::IDLE;
            displayDirty_ = true;
        }
        return;
    }
    if (!recording()) return;

    size_t bytesRead = 0;
    if (i2s_read(MIC_I2S_PORT, rawSamples, sizeof(rawSamples), &bytesRead, 0) != ESP_OK ||
        bytesRead == 0) {
        if (now - startedAt_ >= MAX_SECONDS * 1000UL) finishCapture(now);
        return;
    }

    const size_t sampleCount = bytesRead / sizeof(rawSamples[0]);
    int16_t peak = 0;
    for (size_t index = 0; index < sampleCount; ++index) {
        // INMP441 的有效 24 bit 位于 32 bit 时隙中；沿用实机验证过的缩放方式。
        int32_t converted = rawSamples[index] >> 14;
        converted = constrain(converted, static_cast<int32_t>(INT16_MIN), static_cast<int32_t>(INT16_MAX));
        const int16_t sample = static_cast<int16_t>(converted);
        if (!enqueueSample(sample)) {
            abort(ErrorCode::BUFFER_OVERFLOW);
            return;
        }
        const int16_t magnitude = sample == INT16_MIN ? INT16_MAX : abs(sample);
        if (magnitude > peak) peak = magnitude;
    }

    if (now - lastLevelAt_ >= 100) {
        lastLevelAt_ = now;
        levelPercent_ = static_cast<uint8_t>(min(100, (static_cast<int32_t>(peak) * 100) / 12000));
        displayDirty_ = true;
    }
    if (now - startedAt_ >= MAX_SECONDS * 1000UL) finishCapture(now);
}

bool VoiceCapture::enqueueSample(int16_t sample) {
    if (RING_BYTES - queuedBytes_ < sizeof(sample)) return false;

    const uint8_t bytes[] = {
        static_cast<uint8_t>(sample & 0xFF),
        static_cast<uint8_t>((static_cast<uint16_t>(sample) >> 8) & 0xFF),
    };
    for (uint8_t value : bytes) {
        ring_[writeOffset_] = value;
        writeOffset_ = (writeOffset_ + 1) % RING_BYTES;
    }
    queuedBytes_ += sizeof(sample);
    producedBytes_ += sizeof(sample);
    runningCrc_ = updateCrc32(runningCrc_, bytes, sizeof(bytes));
    return true;
}

void VoiceCapture::abort(ErrorCode error) {
    error_ = error;
    queuedBytes_ = 0;
    readOffset_ = writeOffset_ = 0;
    levelPercent_ = 0;
    stoppedAt_ = millis();
    state_ = State::ERROR_NOTICE;
    displayDirty_ = true;
}

bool VoiceCapture::buildNextPacket(uint8_t *destination, size_t capacity, size_t &length) const {
    length = 0;
    if (destination == nullptr || capacity < 9 || state_ == State::IDLE) return false;

    destination[0] = PACKET_MAGIC;
    destination[1] = PROTOCOL_VERSION;
    writeUint16(destination + 3, sessionId_);

    if (state_ == State::START_NOTICE) {
        destination[2] = static_cast<uint8_t>(PacketType::RECORDING_STARTED);
        writeUint16(destination + 5, SAMPLE_RATE);
        destination[7] = MAX_SECONDS;
        destination[8] = 0;  // 编码 0：PCM16 LE 单声道。
        length = 9;
        return true;
    }
    if (state_ == State::STOP_NOTICE) {
        if (capacity < 15) return false;
        destination[2] = static_cast<uint8_t>(PacketType::RECORDING_STOPPED);
        writeUint32(destination + 5, producedBytes_);
        writeUint32(destination + 9, finalCrc_);
        writeUint16(destination + 13, SAMPLE_RATE);
        length = 15;
        return true;
    }
    if ((state_ == State::RECORDING || state_ == State::DRAINING) && queuedBytes_ > 0) {
        destination[2] = static_cast<uint8_t>(PacketType::AUDIO_CHUNK);
        writeUint32(destination + 5, sentBytes_);
        const size_t chunkBytes = min(capacity - CHUNK_HEADER_BYTES, queuedBytes_);
        for (size_t index = 0; index < chunkBytes; ++index) {
            destination[CHUNK_HEADER_BYTES + index] = ring_[(readOffset_ + index) % RING_BYTES];
        }
        length = CHUNK_HEADER_BYTES + chunkBytes;
        return true;
    }
    if (state_ == State::END_NOTICE) {
        if (capacity < 13) return false;
        destination[2] = static_cast<uint8_t>(PacketType::TRANSFER_END);
        writeUint32(destination + 5, producedBytes_);
        writeUint32(destination + 9, finalCrc_);
        length = 13;
        return true;
    }
    if (state_ == State::ERROR_NOTICE) {
        destination[2] = static_cast<uint8_t>(PacketType::ERROR);
        destination[5] = static_cast<uint8_t>(error_);
        length = 6;
        return true;
    }
    return false;
}

void VoiceCapture::markPacketSent(size_t packetLength) {
    if (state_ == State::START_NOTICE) {
        state_ = State::RECORDING;
        return;
    }
    if (state_ == State::STOP_NOTICE) {
        state_ = queuedBytes_ == 0 ? State::END_NOTICE : State::DRAINING;
        return;
    }
    if ((state_ == State::RECORDING || state_ == State::DRAINING) &&
        packetLength >= CHUNK_HEADER_BYTES) {
        const size_t consumed = min(packetLength - CHUNK_HEADER_BYTES, queuedBytes_);
        readOffset_ = (readOffset_ + consumed) % RING_BYTES;
        queuedBytes_ -= consumed;
        sentBytes_ += consumed;
        if (state_ == State::DRAINING && queuedBytes_ == 0) state_ = State::END_NOTICE;
        return;
    }
    if (state_ == State::END_NOTICE || state_ == State::ERROR_NOTICE) {
        state_ = State::IDLE;
        displayDirty_ = true;
    }
}

uint32_t VoiceCapture::recordedMilliseconds(uint32_t now) const {
    if (startedAt_ == 0) return 0;
    return (recording() ? now : stoppedAt_) - startedAt_;
}

bool VoiceCapture::takeDisplayDirty() {
    const bool dirty = displayDirty_;
    displayDirty_ = false;
    return dirty;
}

void VoiceCapture::resetStream() {
    readOffset_ = 0;
    writeOffset_ = 0;
    queuedBytes_ = 0;
    producedBytes_ = 0;
    sentBytes_ = 0;
    runningCrc_ = 0xFFFFFFFFu;
    finalCrc_ = 0;
    stoppedAt_ = 0;
    levelPercent_ = 0;
}

uint32_t VoiceCapture::updateCrc32(uint32_t crc, const uint8_t *data, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

void VoiceCapture::writeUint16(uint8_t *destination, uint16_t value) {
    destination[0] = value & 0xFF;
    destination[1] = value >> 8;
}

void VoiceCapture::writeUint32(uint8_t *destination, uint32_t value) {
    destination[0] = value & 0xFF;
    destination[1] = (value >> 8) & 0xFF;
    destination[2] = (value >> 16) & 0xFF;
    destination[3] = (value >> 24) & 0xFF;
}
