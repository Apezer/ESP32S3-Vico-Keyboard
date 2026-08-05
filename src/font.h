/**
 * font.h - Bitmap font and icon data declarations
 *
 * Contains bitmap icon data used by the OLED display.
 * All icons are monochrome (1-bit) bitmaps.
 */

#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ===== 16x16 face icons =====
extern const uint8_t smiley_bmp[];      // Smiley face
extern const uint8_t sad_bmp[];         // Sad face
extern const uint8_t surprised_bmp[];   // Surprised face
extern const uint8_t heart_icon[];      // Heart

// ===== 32x32 icons =====
extern const uint8_t wifi_icon[];       // Wi-Fi icon

// ===== Claude Logo =====
extern const uint8_t BT_ICON[];         // Bluetooth icon (8x8)

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
