#include "web_ui.h"
#include "config.h"
#include "sender_stats.h"
#include "sender_task.h"
#include "scenarios.h"
#include "status_led.h"
#include "firmware_version.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

static WebServer s_server(80);
static unsigned long s_last_status_push = 0;

static const char* state_to_string(SenderState s) {
    switch (s) {
        case SS_IDLE: return "IDLE";
        case SS_RUNNING: return "RUNNING";
        case SS_PAUSED: return "PAUSED";
        case SS_COMPLETED: return "COMPLETED";
        case SS_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

static void build_status_json(JsonDocument& doc) {
    doc["state"] = state_to_string(stats_get_state());
    doc["total_lines_sent"] = stats_get_total_lines();
    doc["current_seq"] = stats_get_current_seq();
    doc["actual_rate_hz"] = stats_get_actual_rate_hz();
    doc["target_rate_hz"] = stats_get_target_rate_hz();
    doc["elapsed_sec"] = stats_get_elapsed_sec();
    doc["remaining_sec"] = stats_get_remaining_sec();
    doc["injected_gaps_count"] = stats_get_injected_gaps_count();
    doc["free_heap"] = stats_get_free_heap();
    doc["uptime_sec"] = millis() / 1000;
    doc["ip"] = WiFi.localIP().toString();
    doc["version"] = FIRMWARE_VERSION_STRING;
}

static bool validate_params(SenderScenario sc, const ScenarioParams& p, String& out_error) {
    auto check_rate = [&](float rate, const char* name, float minv, float maxv) -> bool {
        if (rate < minv || rate > maxv) {
            out_error = String(name) + " must be " + String(minv, 1) + "-" + String(maxv, 1) + " Hz";
            return false;
        }
        return true;
    };

    switch (sc) {
        case SC_STEADY:
            if (!check_rate(p.rate_hz, "Rate", 0.1f, 200.0f)) return false;
            break;
        case SC_BURST:
            if (!check_rate(p.burst_rate_hz, "Burst rate", 1.0f, 500.0f)) return false;
            if (p.burst_lines < 1 || p.burst_lines > 10000) {
                out_error = "Burst lines must be 1-10000"; return false;
            }
            if (p.pause_ms < 100 || p.pause_ms > 60000) {
                out_error = "Pause must be 100-60000 ms"; return false;
            }
            break;
        case SC_RAMP:
            if (!check_rate(p.start_rate_hz, "Start rate", 0.1f, 200.0f)) return false;
            if (!check_rate(p.end_rate_hz, "End rate", 0.1f, 200.0f)) return false;
            if (p.ramp_duration_sec < 1 || p.ramp_duration_sec > 259200) {
                out_error = "Ramp duration max 72 hours"; return false;
            }
            break;
        case SC_GAP_INJECT:
            if (!check_rate(p.rate_hz, "Rate", 0.1f, 200.0f)) return false;
            if (p.gap_every_sec < 10 || p.gap_every_sec > 86400) {
                out_error = "Gap interval must be 10-86400 sec"; return false;
            }
            if (p.gap_size < 1 || p.gap_size > 1000) {
                out_error = "Gap size must be 1-1000"; return false;
            }
            break;
        case SC_ENDURANCE:
            if (!check_rate(p.base_rate_hz, "Base rate", 0.1f, 200.0f)) return false;
            if (!check_rate(p.burst_rate_hz, "Burst rate", 1.0f, 500.0f)) return false;
            if (p.burst_lines < 1 || p.burst_lines > 10000) {
                out_error = "Burst lines must be 1-10000"; return false;
            }
            if (p.burst_every_sec < 1 || p.burst_every_sec > 86400) {
                out_error = "Burst interval must be 1-86400 sec"; return false;
            }
            break;
    }

    if (p.duration_sec > 259200) {
        out_error = "Duration max 72 hours"; return false;
    }

    // Rate capacity validation
    float max_rate_hz = 0;
    switch (sc) {
        case SC_STEADY: max_rate_hz = p.rate_hz; break;
        case SC_BURST: max_rate_hz = p.burst_rate_hz; break;
        case SC_RAMP: max_rate_hz = p.end_rate_hz; break;
        case SC_GAP_INJECT: max_rate_hz = p.rate_hz; break;
        case SC_ENDURANCE: max_rate_hz = p.burst_rate_hz; break;
    }
    int avg_line = 50; // medium estimate
    if (p.payload_size == PAYLOAD_SHORT) avg_line = 35;
    else if (p.payload_size == PAYLOAD_MEDIUM) avg_line = 85;
    else if (p.payload_size == PAYLOAD_LONG) avg_line = 200;
    float capacity = (float)UART_BAUDRATE / (float)(avg_line * 10);
    if (max_rate_hz > capacity * 0.9f) {
        out_error = "Rate exceeds UART capacity at current baudrate";
        return false;
    }

    return true;
}

static void handle_start() {
    if (stats_get_state() == SS_RUNNING || stats_get_state() == SS_PAUSED) {
        s_server.send(409, "application/json", "{\"error\":\"Already running. Stop first.\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, s_server.arg("plain"));
    if (err) {
        s_server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const char* scenario_name = doc["scenario"] | "steady";
    SenderScenario sc = SC_STEADY;
    if (strcmp(scenario_name, "steady") == 0) sc = SC_STEADY;
    else if (strcmp(scenario_name, "burst") == 0) sc = SC_BURST;
    else if (strcmp(scenario_name, "ramp") == 0) sc = SC_RAMP;
    else if (strcmp(scenario_name, "gap_inject") == 0) sc = SC_GAP_INJECT;
    else if (strcmp(scenario_name, "endurance") == 0) sc = SC_ENDURANCE;
    else {
        s_server.send(400, "application/json", "{\"error\":\"Invalid scenario\"}");
        return;
    }

    ScenarioParams params;
    JsonObject p = doc["params"];
    if (p) {
        params.rate_hz = p["rate_hz"] | DEFAULT_RATE_HZ;
        params.burst_lines = p["burst_lines"] | 50;
        params.burst_rate_hz = p["burst_rate_hz"] | 100.0f;
        params.pause_ms = p["pause_ms"] | 2000;
        params.start_rate_hz = p["start_rate_hz"] | 1.0f;
        params.end_rate_hz = p["end_rate_hz"] | 100.0f;
        params.ramp_duration_sec = p["ramp_duration_sec"] | 3600;
        params.hold_at_max = p["hold_at_max"] | 0;
        params.gap_every_sec = p["gap_every_sec"] | 300;
        params.gap_size = p["gap_size"] | 5;
        params.base_rate_hz = p["base_rate_hz"] | 10.0f;
        params.burst_every_sec = p["burst_every_sec"] | 600;
        params.duration_sec = p["duration_sec"] | DEFAULT_DURATION_SEC;
        const char* ps = p["payload_size"] | "medium";
        params.payload_size = string_to_payload_size(ps);
    }

    String error_msg;
    if (!validate_params(sc, params, error_msg)) {
        String resp = "{\"error\":\"" + error_msg + "\"}";
        s_server.send(400, "application/json", resp);
        return;
    }

    sender_task_start(sc, params);

    JsonDocument resp;
    resp["ok"] = true;
    resp["scenario"] = scenario_name;
    String resp_str;
    serializeJson(resp, resp_str);
    s_server.send(200, "application/json", resp_str);
}

void web_ui_init() {
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");
    }

    s_server.on("/", HTTP_GET, []() {
        File f = SPIFFS.open("/index.html", "r");
        if (!f) { s_server.send(404, "text/plain", "Not found"); return; }
        s_server.streamFile(f, "text/html");
        f.close();
    });
    s_server.on("/app.js", HTTP_GET, []() {
        File f = SPIFFS.open("/app.js", "r");
        if (!f) { s_server.send(404, "text/plain", "Not found"); return; }
        s_server.streamFile(f, "application/javascript");
        f.close();
    });
    s_server.on("/style.css", HTTP_GET, []() {
        File f = SPIFFS.open("/style.css", "r");
        if (!f) { s_server.send(404, "text/plain", "Not found"); return; }
        s_server.streamFile(f, "text/css");
        f.close();
    });

    s_server.on("/api/status", HTTP_GET, []() {
        JsonDocument doc;
        build_status_json(doc);
        String json;
        serializeJson(doc, json);
        s_server.send(200, "application/json", json);
    });

    s_server.on("/api/start", HTTP_POST, handle_start);

    s_server.on("/api/pause", HTTP_POST, []() {
        if (stats_get_state() != SS_RUNNING) {
            s_server.send(409, "application/json", "{\"error\":\"Not running.\"}");
            return;
        }
        sender_task_pause();
        s_server.send(200, "application/json", "{\"ok\":true}");
    });

    s_server.on("/api/resume", HTTP_POST, []() {
        if (stats_get_state() != SS_PAUSED) {
            s_server.send(409, "application/json", "{\"error\":\"Not paused.\"}");
            return;
        }
        sender_task_resume();
        s_server.send(200, "application/json", "{\"ok\":true}");
    });

    s_server.on("/api/stop", HTTP_POST, []() {
        if (stats_get_state() != SS_RUNNING && stats_get_state() != SS_PAUSED) {
            s_server.send(409, "application/json", "{\"error\":\"Not running.\"}");
            return;
        }
        sender_task_stop();
        s_server.send(200, "application/json", "{\"ok\":true}");
    });

    s_server.on("/api/reset", HTTP_POST, []() {
        if (stats_get_state() == SS_RUNNING || stats_get_state() == SS_PAUSED) {
            s_server.send(409, "application/json", "{\"error\":\"Stop the sender before resetting.\"}");
            return;
        }
        stats_reset_all();
        clear_injected_gaps();
        stats_set_state(SS_IDLE);
        status_led_set_pattern(LED_OFF);
        s_server.send(200, "application/json", "{\"ok\":true}");
    });

    s_server.on("/api/gaps", HTTP_GET, []() {
        size_t count = 0;
        const InjectedGap* gaps = get_injected_gaps(count);
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (size_t i = 0; i < count; i++) {
            JsonObject obj = arr.add<JsonObject>();
            obj["timestamp_sec"] = gaps[i].timestamp_sec;
            obj["seq_before"] = gaps[i].seq_before;
            obj["seq_after"] = gaps[i].seq_after;
            obj["gap_size"] = gaps[i].gap_size;
        }
        String json;
        serializeJson(doc, json);
        s_server.send(200, "application/json", json);
    });

    s_server.on("/api/log", HTTP_GET, []() {
        size_t count = 0;
        const char* const* lines = sender_log_get_lines(count);
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        if (count > 0) {
            size_t start = (SENDER_LOG_MAX_LINES + sender_log_get_head() - count) % SENDER_LOG_MAX_LINES;
            for (size_t i = 0; i < count; i++) {
                size_t idx = (start + i) % SENDER_LOG_MAX_LINES;
                arr.add((const char*)lines[idx]);
            }
        }
        String json;
        serializeJson(doc, json);
        s_server.send(200, "application/json", json);
    });

    s_server.begin();
}

void web_ui_update() {
    s_server.handleClient();
}
