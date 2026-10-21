/**
 * @file oled_animation.cpp
 * @brief 实现 VANI 动画文件校验、SPIFFS A/B 更新和逐帧解码。
 */

#include "oled_animation.h"

#include <Preferences.h>
#include <SPIFFS.h>
#include <cstring>

#include "astronaut_animation.h"

namespace {
constexpr uint8_t VANI_VERSION = 1;
constexpr uint8_t VANI_FLAG_LOOP = 0x01;
constexpr uint8_t ENCODING_RAW = 0;
constexpr uint8_t ENCODING_XOR_RLE = 1;
constexpr uint16_t MIN_FRAME_DELAY_MS = 20;
constexpr uint16_t MAX_FRAME_DELAY_MS = 5000;
constexpr size_t MAX_ENCODED_FRAME_BYTES = 2048;

struct __attribute__((packed)) VaniHeader {
    char magic[4];
    uint8_t version;
    uint8_t width;
    uint8_t height;
    uint8_t flags;
    uint16_t frameCount;
    uint16_t headerBytes;
    uint32_t fileBytes;
    uint32_t totalDurationMs;
    uint32_t reserved;
};

struct __attribute__((packed)) VaniFrameHeader {
    uint16_t delayMs;
    uint8_t encoding;
    uint8_t reserved;
    uint16_t dataLength;
};

static_assert(sizeof(VaniHeader) == 24, "Unexpected VANI header size.");
static_assert(sizeof(VaniFrameHeader) == 6, "Unexpected VANI frame header size.");

bool readExact(fs::File &file, void *destination, size_t length) {
    return file.read(static_cast<uint8_t *>(destination), length) == length;
}
}

bool OledAnimation::begin(uint32_t now) {
    filesystemReady_ = SPIFFS.begin(true);
    loadActiveSlot();
    restart(now);
    update(now);
    return filesystemReady_;
}

bool OledAnimation::restart(uint32_t now) {
    playbackFile_.close();
    std::memset(frame_, 0, sizeof(frame_));
    nextFrameNumber_ = 0;
    nextFrameAt_ = now;

    if (activeValid_ && openPlaybackFile()) {
        usingFallback_ = false;
        return true;
    }

    useFallback(now);
    return false;
}

bool OledAnimation::update(uint32_t now) {
    if (usingFallback_) return updateFallback(now);
    if (static_cast<int32_t>(now - nextFrameAt_) < 0) return false;
    if (readNextStoredFrame(now)) return true;

    // 文件运行时损坏时立即回退内置示例，不让 OLED 停留在半帧画面。
    activeValid_ = false;
    useFallback(now);
    return updateFallback(now, true);
}

AnimationResult OledAnimation::beginUpload(uint32_t totalBytes, uint32_t expectedCrc) {
    if (!filesystemReady_ || totalBytes < sizeof(VaniHeader) || totalBytes > MAX_FILE_BYTES) {
        return AnimationResult::INVALID_SIZE;
    }
    if (uploadActive_) return AnimationResult::BUSY;

    uploadSlot_ = activeValid_ ? static_cast<uint8_t>(1 - activeSlot_) : 0;
    SPIFFS.remove(SLOT_PATHS[uploadSlot_]);
    // 先清理非活动槽，再按 SPIFFS 当前可用空间判断。活动槽绝不会在新文件
    // 校验成功前删除，因此空间不足或传输中断都可以继续播放旧动画。
    if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < totalBytes + 4096) {
        return AnimationResult::NO_SPACE;
    }
    uploadFile_ = SPIFFS.open(SLOT_PATHS[uploadSlot_], FILE_WRITE);
    if (!uploadFile_) return AnimationResult::IO_ERROR;

    uploadActive_ = true;
    uploadExpectedBytes_ = totalBytes;
    uploadReceivedBytes_ = 0;
    uploadExpectedCrc_ = expectedCrc;
    uploadRunningCrc_ = 0xFFFFFFFFu;
    return AnimationResult::OK;
}

AnimationResult OledAnimation::writeUploadChunk(
    uint32_t offset,
    const uint8_t *data,
    size_t length
) {
    if (!uploadActive_) return AnimationResult::NO_UPLOAD;
    if (data == nullptr || length == 0 ||
        offset != uploadReceivedBytes_ ||
        uploadReceivedBytes_ + length > uploadExpectedBytes_) {
        return AnimationResult::OFFSET_MISMATCH;
    }
    if (uploadFile_.write(data, length) != length) {
        cancelUpload();
        return AnimationResult::IO_ERROR;
    }

    uploadRunningCrc_ = updateCrc32(uploadRunningCrc_, data, length);
    uploadReceivedBytes_ += length;
    return AnimationResult::OK;
}

AnimationResult OledAnimation::commitUpload() {
    if (!uploadActive_) return AnimationResult::NO_UPLOAD;
    uploadFile_.flush();
    uploadFile_.close();

    const uint32_t finalCrc = ~uploadRunningCrc_;
    if (uploadReceivedBytes_ != uploadExpectedBytes_ || finalCrc != uploadExpectedCrc_) {
        SPIFFS.remove(SLOT_PATHS[uploadSlot_]);
        uploadActive_ = false;
        return AnimationResult::CRC_MISMATCH;
    }
    if (!validateFile(SLOT_PATHS[uploadSlot_], finalCrc)) {
        SPIFFS.remove(SLOT_PATHS[uploadSlot_]);
        uploadActive_ = false;
        return AnimationResult::INVALID_FORMAT;
    }

    activeSlot_ = uploadSlot_;
    activeValid_ = true;
    activeSize_ = uploadExpectedBytes_;
    activeCrc_ = finalCrc;
    ++revision_;
    uploadActive_ = false;
    saveActiveSlot();
    restart(millis());
    return AnimationResult::OK;
}

void OledAnimation::cancelUpload() {
    if (!uploadActive_) return;
    uploadFile_.close();
    SPIFFS.remove(SLOT_PATHS[uploadSlot_]);
    uploadActive_ = false;
    uploadExpectedBytes_ = 0;
    uploadReceivedBytes_ = 0;
}

AnimationResult OledAnimation::deleteStoredAnimation(uint32_t now) {
    cancelUpload();
    playbackFile_.close();
    if (filesystemReady_) {
        SPIFFS.remove(SLOT_PATHS[0]);
        SPIFFS.remove(SLOT_PATHS[1]);
    }
    activeValid_ = false;
    activeSize_ = 0;
    activeCrc_ = 0;
    activeSlot_ = 0;
    ++revision_;

    Preferences preferences;
    if (preferences.begin("vico_anim", false)) {
        preferences.clear();
        preferences.end();
    }
    useFallback(now);
    updateFallback(now, true);
    return filesystemReady_ ? AnimationResult::OK : AnimationResult::IO_ERROR;
}

bool OledAnimation::loadActiveSlot() {
    activeValid_ = false;
    activeSize_ = 0;
    activeCrc_ = 0;
    if (!filesystemReady_) return false;

    Preferences preferences;
    if (!preferences.begin("vico_anim", true)) return false;
    const uint8_t preferred = preferences.getUChar("active", 0) & 1;
    const uint32_t crcs[2] = {
        preferences.getUInt("crc_a", 0),
        preferences.getUInt("crc_b", 0),
    };
    preferences.end();

    const uint8_t candidates[2] = {preferred, static_cast<uint8_t>(1 - preferred)};
    for (uint8_t candidate : candidates) {
        uint32_t size = 0;
        if (crcs[candidate] != 0 && validateFile(SLOT_PATHS[candidate], crcs[candidate], &size)) {
            activeSlot_ = candidate;
            activeValid_ = true;
            activeSize_ = size;
            activeCrc_ = crcs[candidate];
            return true;
        }
    }
    return false;
}

bool OledAnimation::validateFile(
    const char *path,
    uint32_t expectedCrc,
    uint32_t *sizeOut
) const {
    fs::File file = SPIFFS.open(path, FILE_READ);
    if (!file) return false;
    const uint32_t fileSize = file.size();
    if (fileSize < sizeof(VaniHeader) || fileSize > MAX_FILE_BYTES) return false;

    uint8_t crcBuffer[256];
    uint32_t crc = 0xFFFFFFFFu;
    while (file.available()) {
        const size_t read = file.read(crcBuffer, sizeof(crcBuffer));
        if (read == 0) return false;
        crc = updateCrc32(crc, crcBuffer, read);
    }
    if (~crc != expectedCrc || !file.seek(0)) return false;

    VaniHeader header = {};
    if (!readExact(file, &header, sizeof(header)) ||
        std::memcmp(header.magic, "VANI", 4) != 0 ||
        header.version != VANI_VERSION || header.width != 128 || header.height != 64 ||
        (header.flags & VANI_FLAG_LOOP) == 0 ||
        header.headerBytes != sizeof(VaniHeader) || header.fileBytes != fileSize ||
        header.frameCount == 0 || header.frameCount > MAX_FRAME_COUNT) {
        return false;
    }

    for (uint16_t frameIndex = 0; frameIndex < header.frameCount; ++frameIndex) {
        VaniFrameHeader frameHeader = {};
        if (!readExact(file, &frameHeader, sizeof(frameHeader)) ||
            frameHeader.delayMs < MIN_FRAME_DELAY_MS ||
            frameHeader.delayMs > MAX_FRAME_DELAY_MS ||
            frameHeader.dataLength == 0 ||
            frameHeader.dataLength > MAX_ENCODED_FRAME_BYTES ||
            (frameIndex == 0 &&
                (frameHeader.encoding != ENCODING_RAW || frameHeader.dataLength != FRAME_BYTES)) ||
            (frameIndex > 0 && frameHeader.encoding != ENCODING_RAW &&
                frameHeader.encoding != ENCODING_XOR_RLE)) {
            return false;
        }
        const uint32_t nextPosition = file.position() + frameHeader.dataLength;
        if (nextPosition > fileSize || !file.seek(nextPosition)) return false;
    }
    if (file.position() != fileSize) return false;
    if (sizeOut != nullptr) *sizeOut = fileSize;
    return true;
}

bool OledAnimation::openPlaybackFile() {
    playbackFile_ = SPIFFS.open(SLOT_PATHS[activeSlot_], FILE_READ);
    if (!playbackFile_) return false;
    VaniHeader header = {};
    if (!readExact(playbackFile_, &header, sizeof(header)) ||
        std::memcmp(header.magic, "VANI", 4) != 0 ||
        header.headerBytes != sizeof(VaniHeader)) {
        playbackFile_.close();
        return false;
    }
    frameCount_ = header.frameCount;
    nextFrameNumber_ = 0;
    return true;
}

bool OledAnimation::readNextStoredFrame(uint32_t now) {
    if (!playbackFile_ || frameCount_ == 0) return false;
    if (nextFrameNumber_ >= frameCount_) {
        if (!playbackFile_.seek(sizeof(VaniHeader))) return false;
        nextFrameNumber_ = 0;
        std::memset(frame_, 0, sizeof(frame_));
    }

    VaniFrameHeader header = {};
    if (!readExact(playbackFile_, &header, sizeof(header))) return false;

    if (header.encoding == ENCODING_RAW) {
        if (header.dataLength != FRAME_BYTES ||
            !readExact(playbackFile_, frame_, FRAME_BYTES)) return false;
    } else if (header.encoding == ENCODING_XOR_RLE) {
        size_t outputOffset = 0;
        size_t consumed = 0;
        while (consumed < header.dataLength && outputOffset < FRAME_BYTES) {
            const int controlValue = playbackFile_.read();
            if (controlValue < 0) return false;
            ++consumed;
            const uint8_t control = static_cast<uint8_t>(controlValue);
            const size_t count = (control & 0x7F) + 1;
            if (outputOffset + count > FRAME_BYTES) return false;

            if ((control & 0x80) != 0) {
                outputOffset += count;
                continue;
            }
            if (consumed + count > header.dataLength) return false;
            for (size_t index = 0; index < count; ++index) {
                const int value = playbackFile_.read();
                if (value < 0) return false;
                frame_[outputOffset++] ^= static_cast<uint8_t>(value);
            }
            consumed += count;
        }
        if (consumed != header.dataLength || outputOffset != FRAME_BYTES) return false;
    } else {
        return false;
    }

    ++nextFrameNumber_;
    nextFrameAt_ = now + header.delayMs;
    return true;
}

bool OledAnimation::updateFallback(uint32_t now, bool force) {
    const uint8_t next = AstronautAnimation::frameIndex(now - fallbackStartedAt_);
    if (!force && next == fallbackFrame_) return false;
    fallbackFrame_ = next;
    memcpy_P(frame_, AstronautAnimation::frameData(next), FRAME_BYTES);
    return true;
}

void OledAnimation::useFallback(uint32_t now) {
    playbackFile_.close();
    usingFallback_ = true;
    fallbackStartedAt_ = now;
    fallbackFrame_ = 0xFF;
}

void OledAnimation::saveActiveSlot() const {
    Preferences preferences;
    if (!preferences.begin("vico_anim", false)) return;
    preferences.putUChar("active", activeSlot_);
    preferences.putUInt(activeSlot_ == 0 ? "crc_a" : "crc_b", activeCrc_);
    preferences.end();
}

uint32_t OledAnimation::updateCrc32(
    uint32_t crc,
    const uint8_t *data,
    size_t length
) {
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}
