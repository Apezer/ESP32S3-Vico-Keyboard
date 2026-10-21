#pragma once

/**
 * @file oled_animation.h
 * @brief 管理 SPIFFS 动画双槽存储、VANI 解码和非阻塞 OLED 播放。
 */

#include <Arduino.h>
#include <FS.h>

/** @brief USB 动画上传命令使用的稳定结果码。 */
enum class AnimationResult : uint8_t {
    OK = 0,
    INVALID_SIZE = 1,
    BUSY = 2,
    IO_ERROR = 3,
    OFFSET_MISMATCH = 4,
    CRC_MISMATCH = 5,
    INVALID_FORMAT = 6,
    NO_UPLOAD = 7,
    NO_SPACE = 8,
};

/**
 * @brief 单个活动动画播放器。
 *
 * SPIFFS 中保留 A/B 两个槽位。新动画始终写入非活动槽，完整校验后才切换，
 * 因而上传中断、USB 拔出或掉电都不会损坏正在使用的动画。
 */
class OledAnimation {
public:
    static constexpr size_t FRAME_BYTES = 128 * 64 / 8;
    // 默认分区为 1408 KiB。A/B 双槽各限制为 480 KiB，使两个完整动画与
    // SPIFFS 元数据仍处于较安全的占用范围内。
    static constexpr uint32_t MAX_FILE_BYTES = 480UL * 1024UL;
    static constexpr uint16_t MAX_FRAME_COUNT = 600;

    /** @brief 挂载 SPIFFS、恢复活动槽并准备默认动画帧。 */
    bool begin(uint32_t now);

    /** @brief 从第一帧重新开始，返回是否存在有效的用户动画。 */
    bool restart(uint32_t now);
    /** @brief 到达下一帧时间时读取并解码一帧。 */
    bool update(uint32_t now);
    /** @brief 当前已经解码完成的 SSD1306 页优先帧。 */
    const uint8_t *frame() const { return frame_; }

    bool hasStoredAnimation() const { return activeValid_; }
    uint32_t storedSize() const { return activeSize_; }
    uint32_t storedCrc() const { return activeCrc_; }
    uint32_t revision() const { return revision_; }
    bool uploadActive() const { return uploadActive_; }

    AnimationResult beginUpload(uint32_t totalBytes, uint32_t expectedCrc);
    AnimationResult writeUploadChunk(uint32_t offset, const uint8_t *data, size_t length);
    AnimationResult commitUpload();
    void cancelUpload();
    AnimationResult deleteStoredAnimation(uint32_t now);

private:
    static constexpr const char *SLOT_PATHS[2] = {
        "/vico_anim_a.vani",
        "/vico_anim_b.vani",
    };

    fs::File playbackFile_;
    fs::File uploadFile_;
    uint8_t frame_[FRAME_BYTES] = {};

    bool filesystemReady_ = false;
    bool activeValid_ = false;
    uint8_t activeSlot_ = 0;
    uint32_t activeSize_ = 0;
    uint32_t activeCrc_ = 0;
    uint32_t revision_ = 0;

    bool usingFallback_ = true;
    uint32_t fallbackStartedAt_ = 0;
    uint8_t fallbackFrame_ = 0xFF;
    uint16_t frameCount_ = 0;
    uint16_t nextFrameNumber_ = 0;
    uint32_t nextFrameAt_ = 0;

    bool uploadActive_ = false;
    uint8_t uploadSlot_ = 0;
    uint32_t uploadExpectedBytes_ = 0;
    uint32_t uploadReceivedBytes_ = 0;
    uint32_t uploadExpectedCrc_ = 0;
    uint32_t uploadRunningCrc_ = 0xFFFFFFFFu;

    bool loadActiveSlot();
    bool validateFile(const char *path, uint32_t expectedCrc, uint32_t *sizeOut = nullptr) const;
    bool openPlaybackFile();
    bool readNextStoredFrame(uint32_t now);
    bool updateFallback(uint32_t now, bool force = false);
    void useFallback(uint32_t now);
    void saveActiveSlot() const;

    static uint32_t updateCrc32(uint32_t crc, const uint8_t *data, size_t length);
};
