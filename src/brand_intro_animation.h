#pragma once

#include <Arduino.h>

/**
 * @file brand_intro_animation.h
 * @brief 管理品牌页面开屏动画的非阻塞时间轴。
 *
 * 控制器只维护进入时间和 OLED 重绘节拍，不直接绘图。主循环仍会持续扫描
 * 按键、旋钮并处理 USB/BLE；每到一帧时仅将 display_dirty 置位。
 */
namespace BrandIntroAnimation {

// SSD1306 全帧 I2C 传输本身需要约二十多毫秒，33ms 是接近硬件上限且仍给
// 按键扫描和协议栈留有运行时间的刷新间隔，实际帧率约为 30 FPS。
constexpr uint32_t FRAME_INTERVAL_MS = 33;
constexpr uint32_t DURATION_MS = 1000;

class Controller {
public:
    /** @brief 从第 0 帧开始播放一次开屏动画。 */
    void begin(uint32_t now) {
        active_ = true;
        startedAt_ = now;
        lastFrameAt_ = now - FRAME_INTERVAL_MS;
    }

    /** @brief 页面被其他高优先级画面覆盖时立即停止当前动画。 */
    void stop() {
        active_ = false;
    }

    /**
     * @brief 推进动画并在需要提交下一帧时返回 true。
     *
     * 动画结束时额外请求一次重绘，确保最终静态品牌页完整显示。
     */
    bool update(uint32_t now) {
        if (!active_) return false;
        if (now - startedAt_ >= DURATION_MS) {
            active_ = false;
            lastFrameAt_ = now;
            return true;
        }
        if (now - lastFrameAt_ < FRAME_INTERVAL_MS) return false;
        lastFrameAt_ = now;
        return true;
    }

    bool active() const { return active_; }

    /** @brief 返回当前动画已经播放的毫秒数。 */
    uint32_t elapsed(uint32_t now) const {
        if (!active_) return DURATION_MS;
        const uint32_t value = now - startedAt_;
        return value > DURATION_MS ? DURATION_MS : value;
    }

private:
    bool active_ = false;
    uint32_t startedAt_ = 0;
    uint32_t lastFrameAt_ = 0;
};

} // namespace BrandIntroAnimation
