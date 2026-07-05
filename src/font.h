/**
 * font.h - 字模数据声明
 *
 * 包含 OLED 显示用的位图图标数据
 * 所有图标均为单色 (1-bit) 位图
 */

#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ===== 16x16 表情图标 =====
extern const uint8_t smiley_bmp[];      // 笑脸
extern const uint8_t sad_bmp[];         // 悲伤脸
extern const uint8_t surprised_bmp[];   // 惊讶脸
extern const uint8_t heart_icon[];      // 心形

// ===== 32x32 图标 =====
extern const uint8_t wifi_icon[];       // WiFi 图标

// ===== Claude Logo =====
extern const uint8_t BT_ICON[];         // 蓝牙图标 8x8

#define CLAUDE_LOGO_WIDTH 64
#define CLAUDE_LOGO_HEIGHT 32
extern const uint8_t CLAUDE_LOGO_64X32[];

#define CLAUDE_LOGO_SMALL_WIDTH 32
#define CLAUDE_LOGO_SMALL_HEIGHT 16
extern const uint8_t CLAUDE_LOGO_32X16[];

#ifdef __cplusplus
}
#endif

#endif /* FONT_H */
