#pragma once

#include <Arduino.h>
#include "config.h"

enum LedPattern {
    LED_OFF = 0,
    LED_ON,
    LED_BLINK_SLOW,      // 1 Hz
    LED_BLINK_FAST,      // 5 Hz
    LED_DOUBLE_BLINK     // every 2 sec
};

void status_led_init();
void status_led_set_pattern(LedPattern pattern);
void status_led_update(); // call from loop()
