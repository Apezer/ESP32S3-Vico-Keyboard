/**
 * font.h - 字模与图标数据声明
 *
 * 包含 OLED 显示所使用的位图图标数据。
 * 所有图标均为单色（1 位）位图。
 */

#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ===== 16×16 表情图标 =====
extern const uint8_t smiley_bmp[];      // 笑脸
extern const uint8_t sad_bmp[];         // 悲伤脸
extern const uint8_t surprised_bmp[];   // 惊讶脸
extern const uint8_t heart_icon[];      // 心形

// ===== 32×32 图标 =====
extern const uint8_t wifi_icon[];       // Wi-Fi 图标

// ===== Claude 标志 =====
extern const uint8_t BT_ICON[];         // 蓝牙图标（8×8）

#define CLAUDE_LOGO_WIDTH 64
#define CLAUDE_LOGO_HEIGHT 32
extern const uint8_t CLAUDE_LOGO_64X32[];

#define CLAUDE_LOGO_SMALL_WIDTH 32
#define CLAUDE_LOGO_SMALL_HEIGHT 16
extern const uint8_t CLAUDE_LOGO_32X16[];

#ifdef __cplusplus
}
#endif

#endif /* FONT_H 结束 */
