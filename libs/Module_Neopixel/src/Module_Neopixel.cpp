#include "Module_Neopixel.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#define RGB_PIN 38
#define NUM_PIXELS 1
#define PIXEL_IDX 0
#define BRIGHTNESS_DEFAULT 100

Adafruit_NeoPixel pixel(NUM_PIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);

/* RGB color table */
enum {
  NEO_COLOR_RED,
  NEO_COLOR_GREEN,
  NEO_COLOR_BLUE,
  NEO_COLOR_YELLOW,
  NEO_COLOR_MAGENTA,
  NEO_COLOR_CYAN,
  NEO_COLOR_WHITE,
  NEO_COLOR_OFF,
  NEO_COLOR_COUNT
};

static const uint8_t neo_colors[NEO_COLOR_COUNT][3] = {
    {255, 0, 0},     // red
    {0, 255, 0},     // green
    {0, 0, 255},     // blue
    {255, 255, 0},   // yellow
    {255, 0, 255},   // magenta
    {0, 255, 255},   // cyan
    {255, 255, 255}, // white
    {0, 0, 0}        // off
};

static void neopixel_set_color(uint8_t color_index, uint8_t brightness) {
  pixel.setBrightness(brightness);
  pixel.setPixelColor(PIXEL_IDX, pixel.Color(neo_colors[color_index][0],
                                             neo_colors[color_index][1],
                                             neo_colors[color_index][2]));
  pixel.show();
}

void neopixel_setup(void) {
  pixel.begin();
  pixel.clear();
  pixel.setBrightness(BRIGHTNESS_DEFAULT);
  pixel.show();
}

void neopixel_loop(void) {
  static uint16_t hue = 43690;
  static int8_t dir = 1;
  static uint8_t brightness = 20;

  brightness += dir;
  if (brightness >= 80 || brightness <= 10) {
    dir = -dir;
  }

  pixel.setBrightness(brightness);
  pixel.setPixelColor(PIXEL_IDX, pixel.ColorHSV(hue));
  pixel.show();

  delay(30);
}

void neopixel_success(void) {
  neopixel_set_color(NEO_COLOR_GREEN, BRIGHTNESS_DEFAULT);
}

void neopixel_error(void) {
  neopixel_set_color(NEO_COLOR_RED, BRIGHTNESS_DEFAULT);
}

void neopixel_warning(void) {
  neopixel_set_color(NEO_COLOR_YELLOW, BRIGHTNESS_DEFAULT);
}

void neopixel_stop(void) {
  neopixel_set_color(NEO_COLOR_OFF, BRIGHTNESS_DEFAULT);
}
