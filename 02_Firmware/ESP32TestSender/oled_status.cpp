#include "oled_status.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "config.h"
#include "firmware_version.h"
#include "sender_stats.h"
#include "sender_task.h"
#include "wifi_manager.h"

namespace {

Adafruit_SSD1306 s_display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);
bool s_oled_ready = false;
uint32_t s_last_render_ms = 0;

const char* state_label(SenderState state) {
    switch (state) {
        case SS_IDLE: return "IDLE";
        case SS_RUNNING: return "RUN";
        case SS_PAUSED: return "PAUSE";
        case SS_COMPLETED: return "DONE";
        case SS_ERROR: return "ERROR";
    }
    return "UNK";
}

const char* scenario_label(SenderScenario scenario) {
    switch (scenario) {
        case SC_STEADY: return "STEADY";
        case SC_BURST: return "BURST";
        case SC_RAMP: return "RAMP";
        case SC_GAP_INJECT: return "GAP";
        case SC_ENDURANCE: return "ENDUR";
    }
    return "N/A";
}

void format_hhmmss(uint32_t total_sec, char* out, size_t out_size) {
    uint32_t hours = total_sec / 3600UL;
    uint32_t minutes = (total_sec % 3600UL) / 60UL;
    uint32_t seconds = total_sec % 60UL;
    snprintf(out, out_size, "%02lu:%02lu:%02lu",
             (unsigned long)hours,
             (unsigned long)minutes,
             (unsigned long)seconds);
}

void draw_line(uint8_t row, const char* text) {
    s_display.setCursor(0, row * 8);
    s_display.println(text);
}

void render_status() {
    char line[32];
    char elapsed_text[16];
    char ip_text[24];
    const String ip = wifi_manager_ip();

    format_hhmmss(stats_get_elapsed_sec(), elapsed_text, sizeof(elapsed_text));
    snprintf(ip_text, sizeof(ip_text), "%s", ip.c_str());

    s_display.clearDisplay();
    s_display.setTextSize(1);
    s_display.setTextColor(SSD1306_WHITE);

    snprintf(line, sizeof(line), "SENDER v%s", FIRMWARE_VERSION_STRING);
    draw_line(0, line);

    snprintf(line, sizeof(line), "%s %s",
             state_label(stats_get_state()),
             scenario_label(g_active_scenario));
    draw_line(1, line);

    snprintf(line, sizeof(line), "SEQ %lu LN %lu",
             (unsigned long)stats_get_current_seq(),
             (unsigned long)stats_get_total_lines());
    draw_line(2, line);

    snprintf(line, sizeof(line), "RATE %.1f/%.1f",
             (double)stats_get_actual_rate_hz(),
             (double)stats_get_target_rate_hz());
    draw_line(3, line);

    snprintf(line, sizeof(line), "ELP %s", elapsed_text);
    draw_line(4, line);

    snprintf(line, sizeof(line), "%s %s",
             wifi_manager_mode_label(),
             ip_text);
    draw_line(5, line);

    if (stats_get_injected_gaps_count() > 0) {
        snprintf(line, sizeof(line), "GAPS %lu",
                 (unsigned long)stats_get_injected_gaps_count());
    } else {
        snprintf(line, sizeof(line), "%s.local", wifi_manager_hostname());
    }
    draw_line(6, line);

    s_display.display();
}

} // namespace

void oled_status_init() {
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    s_oled_ready = s_display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS);
    if (!s_oled_ready) {
        Serial.println("[OLED] SSD1306 init failed");
        return;
    }

    s_display.clearDisplay();
    s_display.setTextSize(1);
    s_display.setTextColor(SSD1306_WHITE);
    draw_line(0, "ESP32 Test Sender");
    draw_line(1, "OLED ready");
    s_display.display();

    s_last_render_ms = 0;
}

void oled_status_update() {
    if (!s_oled_ready) {
        return;
    }

    uint32_t now_ms = millis();
    if (s_last_render_ms != 0 && (now_ms - s_last_render_ms) < OLED_REFRESH_MS) {
        return;
    }

    s_last_render_ms = now_ms;
    render_status();
}

bool oled_status_is_ready() {
    return s_oled_ready;
}
