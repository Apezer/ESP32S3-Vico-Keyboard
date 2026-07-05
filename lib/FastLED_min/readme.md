# FastLED_min

**Minimal WS2812B LED Library for ESP32**

A lightweight alternative to FastLED, using the ESP32 RMT peripheral for precise timing.

## Features

✅ **Memory Efficient**: ~297KB vs FastLED's ~380KB (saves 83KB!)  
✅ **ESP32 Hardware RMT**: Precise timing without bit-banging  
✅ **FastLED-Compatible API**: Easy migration from FastLED  
✅ **Proven Timing**: Based on FastLED's tested WS2812B values  
✅ **Named Colors**: 11 common colors included  

## Supported LEDs

- WS2812B
- WS2812
- WS2811 (compatible)

## Installation

### Method 1: Arduino Library Manager (Coming Soon)
1. Open Arduino IDE
2. Go to: Sketch → Include Library → Manage Libraries
3. Search for "FastLED_min"
4. Click Install

### Method 2: Manual Installation
1. Download the [latest release](https://github.com/yourusername/FastLED_min/releases)
2. Extract to your Arduino libraries folder:
   - Windows: `Documents\Arduino\libraries\FastLED_min\`
   - Mac: `~/Documents/Arduino/libraries/FastLED_min/`
   - Linux: `~/Arduino/libraries/FastLED_min/`
3. Restart Arduino IDE

### Method 3: Git Clone
```bash
cd ~/Documents/Arduino/libraries/
git clone https://github.com/yourusername/FastLED_min.git
```

## Quick Start

```cpp
#include <FastLED_min.h>

#define LED_PIN 15
#define NUM_LEDS 1
#define BRIGHTNESS 50

CRGB leds[NUM_LEDS];

void setup() {
  FASTLED_MIN_SETUP(LED_PIN, leds, NUM_LEDS);
  FastLED_min<LED_PIN>.setBrightness(BRIGHTNESS);
}

void loop() {
  leds[0] = CRGB::Red;
  FastLED_min<LED_PIN>.show();
  delay(1000);
  
  leds[0] = CRGB::Blue;
  FastLED_min<LED_PIN>.show();
  delay(1000);
}
```

## Wiring

```
ESP32 GPIO 15 → WS2812B Data In
ESP32 GND → WS2812B GND
5V Power Supply → WS2812B VCC (5V)
5V Power Supply GND → ESP32 GND (common ground)
```

⚠️ **Important**: WS2812B LEDs need 5V power. Connect a separate 5V supply for the LEDs.

## Examples

See the [examples folder](examples/) for:
- Basic color demo
- Rainbow cycle
- Color fading
- Multi-LED effects

## API Reference

### Setup
```cpp
CRGB leds[NUM_LEDS];
FASTLED_MIN_SETUP(PIN, leds, NUM_LEDS);
```

### Colors
```cpp
leds[0] = CRGB::Red;          // Named colors
leds[0] = CRGB(255, 128, 0);  // Custom RGB
leds[0].r = 200;               // Individual channels
```

### Control
```cpp
FastLED_min<PIN>.show();              // Update LEDs
FastLED_min<PIN>.setBrightness(128);  // Set brightness (0-255)
FastLED_min<PIN>.clear();             // Clear all LEDs
```

### Named Colors
`Black`, `Red`, `Green`, `Blue`, `Yellow`, `Cyan`, `Magenta`, `White`, `Orange`, `Purple`, `Pink`

## Memory Comparison

| Library | Flash Used | RAM Used | Savings |
|---------|-----------|----------|---------|
| FastLED | ~380KB | ~15KB | - |
| **FastLED_min** | **~297KB** | **~8KB** | **83KB** |

## Hardware Requirements

- **ESP32** (any variant - ESP32, ESP32-S2, ESP32-S3, ESP32-C3)
- **WS2812B LEDs** or compatible
- **5V power supply** for LEDs (separate from ESP32)

## Limitations

- ESP32 only (uses ESP32-specific RMT peripheral)
- Single GPIO pin per instance
- No advanced effects (keeps library minimal)

## Migrating from FastLED

Most FastLED code works with minimal changes:

```cpp
// FastLED
#include <FastLED.h>
FastLED.addLeds<WS2812B, PIN>(leds, NUM_LEDS);
FastLED.show();
FastLED.setBrightness(50);

// FastLED_min
#include <FastLED_min.h>
FASTLED_MIN_SETUP(PIN, leds, NUM_LEDS);
FastLED_min<PIN>.show();
FastLED_min<PIN>.setBrightness(50);
```

## Contributing

Contributions welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Submit a pull request

## License

MIT License - see [LICENSE](LICENSE) file

## Credits

Based on FastLED's proven RMT implementation and timing values.

## Support

- **Issues**: [GitHub Issues](https://github.com/yourusername/FastLED_min/issues)
- **Discussions**: [GitHub Discussions](https://github.com/yourusername/FastLED_min/discussions)

## Changelog

### v1.0.0 (2025-09-30)
- Initial release
- Basic WS2812B support
- FastLED-compatible API
- 11 named colors
- Example sketches