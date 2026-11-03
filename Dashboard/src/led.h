#pragma once
#include <Adafruit_NeoPixel.h>

enum LedState {
  LED_GREEN_OK,
  LED_BLUE_USER,
  LED_RED_FAIL
};

extern Adafruit_NeoPixel pixel;

void initLed();
void setLed(LedState state);

// precte fatalError/needsUserAttention/atd a vybarvi LED odpovidajicim
// zpusobem - vola se po kazde zmene relevantniho stavu
void updateLedFromState();
