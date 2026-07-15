/*
 * FastLED_min Example - Rainbow Color Cycle
 * 
 * Hardware:
 * - ESP32 (any variant)
 * - WS2812B LED(s) connected to GPIO 15
 * - 5V power supply for LEDs
 * 
 * Installation:
 * 1. Create folder: Documents/Arduino/libraries/FastLED_min/
 * 2. Place FastLED_min.h in that folder
 * 3. Restart Arduino IDE
 * 4. Upload this example
 * 
 * Memory Usage: ~297KB (vs FastLED ~380KB - saves 83KB!)
 */

#include <FastLED_min.h>

#define LED_PIN 15       // GPIO pin for data
#define NUM_LEDS 1       // Number of LEDs in your strip
#define BRIGHTNESS 50    // 0-255 (50 = ~20% brightness)

// Create LED array
CRGB leds[NUM_LEDS];

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=== FastLED_min Example ===");
  Serial.printf("Pin: %d | LEDs: %d | Brightness: %d\n", LED_PIN, NUM_LEDS, BRIGHTNESS);
  
  // Initialize FastLED_min (similar to FastLED API)
  FASTLED_MIN_SETUP(LED_PIN, leds, NUM_LEDS);
  FastLED_min<LED_PIN>.setBrightness(BRIGHTNESS);
  
  Serial.println("Ready!");
}

void loop() {
  // Example 1: Basic colors with named constants
  Serial.println("Testing basic colors...");
  testBasicColors();
  delay(2000);
  
  // Example 2: Rainbow cycle
  Serial.println("Rainbow cycle...");
  rainbowCycle(10);
  delay(1000);
  
  // Example 3: Color fade
  Serial.println("Color fade...");
  colorFade();
  delay(1000);
}

// Test all basic named colors
void testBasicColors() {
  CRGB colors[] = {
    CRGB::Red,
    CRGB::Green, 
    CRGB::Blue,
    CRGB::Yellow,
    CRGB::Cyan,
    CRGB::Magenta,
    CRGB::White,
    CRGB::Orange,
    CRGB::Purple,
    CRGB::Pink
  };
  
  const char* names[] = {
    "Red", "Green", "Blue", "Yellow", "Cyan",
    "Magenta", "White", "Orange", "Purple", "Pink"
  };
  
  for (int i = 0; i < 10; i++) {
    Serial.printf("  %s\n", names[i]);
    leds[0] = colors[i];
    FastLED_min<LED_PIN>.show();
    delay(500);
    
    // Brief off period
    leds[0] = CRGB::Black;
    FastLED_min<LED_PIN>.show();
    delay(200);
  }
}

// Rainbow color cycle
void rainbowCycle(int cycles) {
  for (int cycle = 0; cycle < cycles; cycle++) {
    for (int hue = 0; hue < 360; hue += 5) {
      // Simple HSV to RGB conversion
      leds[0] = hsvToRgb(hue, 255, 255);
      FastLED_min<LED_PIN>.show();
      delay(20);
    }
  }
}

// Fade between red, green, and blue
void colorFade() {
  // Red to Green
  for (int i = 0; i <= 255; i += 5) {
    leds[0] = CRGB(255 - i, i, 0);
    FastLED_min<LED_PIN>.show();
    delay(20);
  }
  
  // Green to Blue
  for (int i = 0; i <= 255; i += 5) {
    leds[0] = CRGB(0, 255 - i, i);
    FastLED_min<LED_PIN>.show();
    delay(20);
  }
  
  // Blue to Red
  for (int i = 0; i <= 255; i += 5) {
    leds[0] = CRGB(i, 0, 255 - i);
    FastLED_min<LED_PIN>.show();
    delay(20);
  }
}

// Simple HSV to RGB conversion
CRGB hsvToRgb(int h, uint8_t s, uint8_t v) {
  uint8_t r, g, b;
  
  int region = h / 60;
  int remainder = (h - (region * 60)) * 6;
  
  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
  
  switch (region) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  
  return CRGB(r, g, b);
}

/*
 * Additional Examples:
 * 
 * EXAMPLE: Multi-LED Strip (change NUM_LEDS to your count)
 * 
 * void fillStrip(CRGB color) {
 *   for (int i = 0; i < NUM_LEDS; i++) {
 *     leds[i] = color;
 *   }
 *   FastLED_min<LED_PIN>.show();
 * }
 * 
 * void chaseEffect() {
 *   for (int i = 0; i < NUM_LEDS; i++) {
 *     FastLED_min<LED_PIN>.clear();
 *     leds[i] = CRGB::Red;
 *     FastLED_min<LED_PIN>.show();
 *     delay(50);
 *   }
 * }
 * 
 * EXAMPLE: Breathing effect
 * 
 * void breathe(CRGB color) {
 *   for (int brightness = 0; brightness < 255; brightness += 5) {
 *     FastLED_min<LED_PIN>.setBrightness(brightness);
 *     leds[0] = color;
 *     FastLED_min<LED_PIN>.show();
 *     delay(20);
 *   }
 *   for (int brightness = 255; brightness > 0; brightness -= 5) {
 *     FastLED_min<LED_PIN>.setBrightness(brightness);
 *     leds[0] = color;
 *     FastLED_min<LED_PIN>.show();
 *     delay(20);
 *   }
 * }
 * 
 * EXAMPLE: Direct color assignment
 * 
 * leds[0] = CRGB(255, 128, 64);  // Custom RGB color
 * leds[0].r = 200;                // Modify individual channels
 * leds[0].g = 100;
 * leds[0].b = 50;
 */