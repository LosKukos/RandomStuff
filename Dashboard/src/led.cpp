#include "led.h"
#include "config.h"
#include "state.h"

Adafruit_NeoPixel pixel(PIXEL_COUNT, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

void initLed() {
  pixel.begin();
  pixel.clear();
  pixel.show();
}

void setLed(LedState state) {
  if (!lockMutex(ledMutex)) return;
  switch (state) {
    case LED_GREEN_OK:  pixel.setPixelColor(0, pixel.Color(0,  40, 0));  break;
    case LED_BLUE_USER: pixel.setPixelColor(0, pixel.Color(0,  0,  40)); break;
    case LED_RED_FAIL:  pixel.setPixelColor(0, pixel.Color(40, 0,  0));  break;
  }
  pixel.show();
  unlockMutex(ledMutex);
}

void updateLedFromState() {
  if (getFatalError())              setLed(LED_RED_FAIL);
  else if (getNeedsUserAttention())  setLed(LED_BLUE_USER);
  else                               setLed(LED_GREEN_OK);
}
