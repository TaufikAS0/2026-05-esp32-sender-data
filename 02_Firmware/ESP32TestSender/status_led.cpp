#include "status_led.h"

static LedPattern s_current_pattern = LED_OFF;
static unsigned long s_last_toggle = 0;
static bool s_led_state = false;
static uint8_t s_blink_phase = 0; // for double blink

void status_led_init() {
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    s_current_pattern = LED_OFF;
    s_last_toggle = 0;
    s_led_state = false;
    s_blink_phase = 0;
}

void status_led_set_pattern(LedPattern pattern) {
    if (s_current_pattern == pattern) return;
    s_current_pattern = pattern;
    s_last_toggle = millis();
    s_led_state = false;
    s_blink_phase = 0;
    // Immediate update for ON/OFF
    if (pattern == LED_ON) {
        digitalWrite(STATUS_LED_PIN, HIGH);
    } else if (pattern == LED_OFF) {
        digitalWrite(STATUS_LED_PIN, LOW);
    }
}

void status_led_update() {
    unsigned long now = millis();
    switch (s_current_pattern) {
        case LED_OFF:
            if (s_led_state) {
                s_led_state = false;
                digitalWrite(STATUS_LED_PIN, LOW);
            }
            break;
        case LED_ON:
            if (!s_led_state) {
                s_led_state = true;
                digitalWrite(STATUS_LED_PIN, HIGH);
            }
            break;
        case LED_BLINK_SLOW: {
            // 1 Hz = 500ms on, 500ms off
            if (now - s_last_toggle >= 500) {
                s_last_toggle = now;
                s_led_state = !s_led_state;
                digitalWrite(STATUS_LED_PIN, s_led_state ? HIGH : LOW);
            }
            break;
        }
        case LED_BLINK_FAST: {
            // 5 Hz = 100ms on, 100ms off
            if (now - s_last_toggle >= 100) {
                s_last_toggle = now;
                s_led_state = !s_led_state;
                digitalWrite(STATUS_LED_PIN, s_led_state ? HIGH : LOW);
            }
            break;
        }
        case LED_DOUBLE_BLINK: {
            // Double blink every 2 seconds
            unsigned long cycle = now % 2000;
            bool on = false;
            if (cycle < 100) on = true;
            else if (cycle < 200) on = false;
            else if (cycle < 300) on = true;
            else on = false;
            digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
            break;
        }
    }
}
