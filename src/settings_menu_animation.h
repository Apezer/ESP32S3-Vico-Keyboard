#pragma once

#include <Arduino.h>
#include <cmath>

/**
 * @file settings_menu_animation.h
 * @brief 设置菜单的非阻塞动画状态机。
 *
 * 该模块只计算坐标，不直接操作 OLED。主循环按照 FRAME_INTERVAL_MS 请求重绘，
 * 因而按键扫描、BLE/USB HID 和后台传输不会被动画阻塞。
 */
namespace SettingsMenuAnimation {

constexpr int16_t SCREEN_WIDTH = 128;
constexpr int16_t LIST_TOP = 13;
constexpr int16_t ROW_HEIGHT = 13;
constexpr uint8_t VISIBLE_ROWS = 4;
constexpr uint32_t FRAME_INTERVAL_MS = 28;
constexpr uint32_t PAGE_TRANSITION_MS = 180;

/** @brief 单个菜单页面的选中框与滚动位置。 */
class Motion {
public:
    /** @brief 进入页面时把动画位置对齐到当前选项。 */
    void reset(uint32_t now, uint8_t selection, uint8_t itemCount) {
        updatedAt_ = now;
        scroll_ = targetScroll(selection, itemCount);
        highlightY_ = LIST_TOP + selection * ROW_HEIGHT - scroll_;
        scrollVelocity_ = 0.0f;
        highlightVelocity_ = 0.0f;
    }

    /** @brief 推进弹簧动画；长时间停顿后限制积分量，防止坐标突然越界。 */
    void update(uint32_t now, uint8_t selection, uint8_t itemCount) {
        uint32_t remaining = now - updatedAt_;
        updatedAt_ = now;
        if (remaining > 64) remaining = 64;

        const float scrollTarget = targetScroll(selection, itemCount);
        const float highlightTarget = LIST_TOP + selection * ROW_HEIGHT - scrollTarget;
        while (remaining > 0) {
            const uint32_t step = remaining > 8 ? 8 : remaining;
            const float seconds = step * 0.001f;
            spring(scroll_, scrollVelocity_, scrollTarget, seconds);
            spring(highlightY_, highlightVelocity_, highlightTarget, seconds);
            remaining -= step;
        }
    }

    int16_t scroll() const {
        return static_cast<int16_t>(scroll_ + 0.5f);
    }

    int16_t highlightY() const {
        return static_cast<int16_t>(highlightY_ + 0.5f);
    }

    bool settled(uint8_t selection, uint8_t itemCount) const {
        const float scrollTarget = targetScroll(selection, itemCount);
        const float highlightTarget = LIST_TOP + selection * ROW_HEIGHT - scrollTarget;
        return fabsf(scroll_ - scrollTarget) < 0.05f &&
               fabsf(highlightY_ - highlightTarget) < 0.05f &&
               fabsf(scrollVelocity_) < 0.1f &&
               fabsf(highlightVelocity_) < 0.1f;
    }

private:
    static float targetScroll(uint8_t selection, uint8_t itemCount) {
        if (itemCount <= VISIBLE_ROWS) return 0.0f;

        // 尽量让当前选项位于第二行；首尾处自然贴边，避免四项一组的翻页跳变。
        int16_t rows = static_cast<int16_t>(selection) - 1;
        if (rows < 0) rows = 0;
        const int16_t maxRows = itemCount - VISIBLE_ROWS;
        if (rows > maxRows) rows = maxRows;
        return rows * ROW_HEIGHT;
    }

    static void spring(float &value, float &velocity, float target, float seconds) {
        // 临界阻尼附近的参数让小屏动画迅速收敛，同时保留轻微的物理跟随感。
        velocity += ((target - value) * 220.0f - velocity * 26.0f) * seconds;
        value += velocity * seconds;
        if (fabsf(target - value) < 0.03f && fabsf(velocity) < 0.1f) {
            value = target;
            velocity = 0.0f;
        }
    }

    float scroll_ = 0.0f;
    float highlightY_ = LIST_TOP;
    float scrollVelocity_ = 0.0f;
    float highlightVelocity_ = 0.0f;
    uint32_t updatedAt_ = 0;
};

/** @brief 管理列表动画、页面滑动以及 OLED 重绘节拍。 */
class Controller {
public:
    void begin(uint32_t now, uint8_t selection, uint8_t itemCount) {
        motion_.reset(now, selection, itemCount);
        transitionActive_ = false;
        lastFrameAt_ = now - FRAME_INTERVAL_MS;
    }

    /**
     * @brief 启动页面切换。
     * @param direction 1 表示进入子页面，-1 表示返回上级页面。
     */
    void startPageTransition(
        uint32_t now,
        uint8_t fromScreen,
        uint8_t fromSelection,
        uint8_t fromItemCount,
        uint8_t toSelection,
        uint8_t toItemCount,
        int8_t direction
    ) {
        fromScreen_ = fromScreen;
        fromSelection_ = fromSelection;
        fromScroll_ = motion_.scroll();
        fromHighlightY_ = motion_.highlightY();
        if (fromItemCount == 0) {
            fromScroll_ = 0;
            fromHighlightY_ = LIST_TOP;
        }

        direction_ = direction >= 0 ? 1 : -1;
        transitionStartedAt_ = now;
        transitionActive_ = true;
        motion_.reset(now, toSelection, toItemCount);
        lastFrameAt_ = now - FRAME_INTERVAL_MS;
    }

    /** @brief 推进动画并在到达下一帧时返回 true。 */
    bool update(uint32_t now, uint8_t selection, uint8_t itemCount) {
        motion_.update(now, selection, itemCount);
        bool transitionFinished = false;
        if (transitionActive_ && now - transitionStartedAt_ >= PAGE_TRANSITION_MS) {
            transitionActive_ = false;
            transitionFinished = true;
        }

        const bool animating = transitionActive_ || !motion_.settled(selection, itemCount);
        // 页面滑动结束时必须额外提交一帧，清除屏幕边缘残留的旧页面像素。
        if (transitionFinished) {
            lastFrameAt_ = now;
            return true;
        }
        if (!animating || now - lastFrameAt_ < FRAME_INTERVAL_MS) return false;
        lastFrameAt_ = now;
        return true;
    }

    bool transitionActive() const { return transitionActive_; }
    uint8_t fromScreen() const { return fromScreen_; }
    uint8_t fromSelection() const { return fromSelection_; }
    int16_t fromScroll() const { return fromScroll_; }
    int16_t fromHighlightY() const { return fromHighlightY_; }
    int16_t scroll() const { return motion_.scroll(); }
    int16_t highlightY() const { return motion_.highlightY(); }

    int16_t outgoingOffset(uint32_t now) const {
        return -direction_ * transitionDistance(now);
    }

    int16_t incomingOffset(uint32_t now) const {
        return direction_ * (SCREEN_WIDTH - transitionDistance(now));
    }

private:
    int16_t transitionDistance(uint32_t now) const {
        if (!transitionActive_) return SCREEN_WIDTH;
        const uint32_t elapsed = now - transitionStartedAt_;
        if (elapsed >= PAGE_TRANSITION_MS) return SCREEN_WIDTH;
        const float progress = static_cast<float>(elapsed) / PAGE_TRANSITION_MS;
        // smoothstep 在起点和终点速度均为 0，页面不会突然启动或急停。
        const float eased = progress * progress * (3.0f - 2.0f * progress);
        return static_cast<int16_t>(SCREEN_WIDTH * eased + 0.5f);
    }

    Motion motion_;
    bool transitionActive_ = false;
    int8_t direction_ = 1;
    uint8_t fromScreen_ = 0;
    uint8_t fromSelection_ = 0;
    int16_t fromScroll_ = 0;
    int16_t fromHighlightY_ = LIST_TOP;
    uint32_t transitionStartedAt_ = 0;
    uint32_t lastFrameAt_ = 0;
};

} // namespace SettingsMenuAnimation
