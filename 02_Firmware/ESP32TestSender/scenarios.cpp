#include "scenarios.h"
#include "sender_stats.h"
#include "sender_task.h"
#include "config.h"
#include <esp_timer.h>

static InjectedGap s_gaps[MAX_INJECTED_GAPS];
static size_t s_gap_count = 0;

void scenario_init() {
    s_gap_count = 0;
}

float generate_random_float(float min_val, float max_val) {
    return min_val + (float)random(0, 10001) / 10000.0f * (max_val - min_val);
}

static void build_payload(PayloadSize psize, char* buf, size_t buf_size, uint32_t custom_target_size) {
    if (psize == PAYLOAD_SHORT) {
        float t = generate_random_float(20.0f, 35.0f);
        snprintf(buf, buf_size, "T:%.1f,S:OK", t);
    } else if (psize == PAYLOAD_MEDIUM) {
        float t = generate_random_float(20.0f, 35.0f);
        float h = generate_random_float(30.0f, 80.0f);
        float p = generate_random_float(990.0f, 1030.0f);
        float v = generate_random_float(3.0f, 3.6f);
        snprintf(buf, buf_size, "T:%.1f,H:%.1f,P:%.1f,V:%.2f,S:OK", t, h, p, v);
    } else if (psize == PAYLOAD_LONG) {
        float t = generate_random_float(20.0f, 35.0f);
        float h = generate_random_float(30.0f, 80.0f);
        float p = generate_random_float(990.0f, 1030.0f);
        float v = generate_random_float(3.0f, 3.6f);
        // base medium
        char base[128];
        snprintf(base, sizeof(base), "T:%.1f,H:%.1f,P:%.1f,V:%.2f,S:OK", t, h, p, v);
        int base_len = strlen(base);
        int needed = 200 - base_len - 1;
        if (needed < 0) needed = 0;
        // add random padding
        const char alphanum[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        int alphanum_len = sizeof(alphanum) - 1;
        int pos = snprintf(buf, buf_size, "%s,", base);
        for (int i = 0; i < needed && pos < (int)buf_size - 1; i++) {
            buf[pos++] = alphanum[random(0, alphanum_len)];
        }
        buf[pos] = '\0';
    } else {
        // custom
        float t = generate_random_float(20.0f, 35.0f);
        float h = generate_random_float(30.0f, 80.0f);
        float p = generate_random_float(990.0f, 1030.0f);
        float v = generate_random_float(3.0f, 3.6f);
        char base[128];
        snprintf(base, sizeof(base), "T:%.1f,H:%.1f,P:%.1f,V:%.2f,S:OK", t, h, p, v);
        int base_len = strlen(base);
        int target = (int)custom_target_size;
        if (target < 1) target = 1;
        if (target > 250) target = 250;
        int needed = target - base_len - 1;
        if (needed < 0) {
            // trim base
            strncpy(buf, base, target);
            buf[target] = '\0';
        } else {
            const char alphanum[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
            int alphanum_len = sizeof(alphanum) - 1;
            int pos = snprintf(buf, buf_size, "%s,", base);
            for (int i = 0; i < needed && pos < (int)buf_size - 1; i++) {
                buf[pos++] = alphanum[random(0, alphanum_len)];
            }
            buf[pos] = '\0';
        }
    }
}

void send_line(PayloadSize psize, uint32_t custom_target_size) {
    char payload[256];
    build_payload(psize, payload, sizeof(payload), custom_target_size);

    uint32_t seq = stats_get_current_seq();
    char line[320];
    snprintf(line, sizeof(line), "%lu,%s\n", (unsigned long)seq, payload);

    Serial2.print(line);

    stats_increment_lines();
    stats_increment_seq(1);
}

// Timing helper: non-blocking wait with pause/stop checks
extern volatile bool g_sender_paused;
extern volatile bool g_sender_stop;

static bool wait_until(int64_t target_time_us) {
    while (esp_timer_get_time() < target_time_us) {
        if (g_sender_stop) return false;
        int64_t delta = target_time_us - esp_timer_get_time();
        if (delta > 2000) {
            vTaskDelay(1); // ~1ms
        }
        while (g_sender_paused && !g_sender_stop) {
            vTaskDelay(10);
        }
        if (g_sender_stop) return false;
    }
    return true;
}

static void rate_tracker_init(uint32_t& lines_snapshot, uint32_t& last_check_ms) {
    lines_snapshot = stats_get_total_lines();
    last_check_ms = millis();
}

static void rate_tracker_update(uint32_t& lines_snapshot, uint32_t& last_check_ms) {
    uint32_t now_ms = millis();
    uint32_t elapsed_ms = now_ms - last_check_ms;
    if (elapsed_ms < 1000) {
        return;
    }

    uint32_t total_lines_now = stats_get_total_lines();
    uint32_t delta_lines = total_lines_now - lines_snapshot;
    uint32_t interval_sec = elapsed_ms / 1000;
    if (interval_sec == 0) {
        interval_sec = 1;
    }

    stats_update_actual_rate(delta_lines, interval_sec);
    lines_snapshot = total_lines_now;
    last_check_ms = now_ms;
}

void run_steady(const ScenarioParams& params) {
    float rate_hz = params.rate_hz;
    if (rate_hz < 0.1f) rate_hz = 0.1f;
    int64_t interval_us = (int64_t)(1000000.0 / rate_hz);
    int64_t next_send = esp_timer_get_time();
    uint32_t start_sec = millis() / 1000;
    uint32_t rate_lines_snapshot = 0;
    uint32_t rate_last_check_ms = 0;

    stats_set_target_rate_hz(rate_hz);
    rate_tracker_init(rate_lines_snapshot, rate_last_check_ms);

    while (!g_sender_stop) {
        while (g_sender_paused && !g_sender_stop) {
            vTaskDelay(10);
            rate_tracker_update(rate_lines_snapshot, rate_last_check_ms);
        }
        if (g_sender_stop) break;

        if (!wait_until(next_send)) break;

        send_line(params.payload_size, params.custom_payload_size);
        rate_tracker_update(rate_lines_snapshot, rate_last_check_ms);
        next_send += interval_us;

        // Update remaining
        if (params.duration_sec > 0) {
            uint32_t elapsed = (millis() / 1000) - start_sec;
            stats_set_elapsed_sec(elapsed);
            if (elapsed >= params.duration_sec) {
                break;
            }
            uint32_t rem = (elapsed < params.duration_sec) ? (params.duration_sec - elapsed) : 0;
            stats_set_remaining_sec(rem);
        } else {
            stats_set_elapsed_sec((millis() / 1000) - start_sec);
            stats_set_remaining_sec(0xFFFFFFFF); // infinity marker
        }

        // Catch-up prevention: if next_send is far in the past, reset to now+interval
        int64_t now = esp_timer_get_time();
        if (next_send < now - interval_us) {
            next_send = now + interval_us;
        }
    }
}

void run_burst(const ScenarioParams& params) {
    float burst_rate = params.burst_rate_hz;
    if (burst_rate < 1.0f) burst_rate = 1.0f;
    int64_t burst_interval_us = (int64_t)(1000000.0 / burst_rate);
    uint32_t start_sec = millis() / 1000;
    uint32_t burst_counter = 0;
    uint32_t rate_lines_snapshot = 0;
    uint32_t rate_last_check_ms = 0;

    stats_set_target_rate_hz(burst_rate);
    rate_tracker_init(rate_lines_snapshot, rate_last_check_ms);

    while (!g_sender_stop) {
        // Burst phase
        int64_t burst_start = esp_timer_get_time();
        for (uint32_t i = 0; i < params.burst_lines; i++) {
            if (g_sender_stop) break;
            while (g_sender_paused && !g_sender_stop) {
                vTaskDelay(10);
                rate_tracker_update(rate_lines_snapshot, rate_last_check_ms);
            }
            if (g_sender_stop) break;

            int64_t next_send = burst_start + (int64_t)burst_interval_us * i;
            if (!wait_until(next_send)) break;
            send_line(params.payload_size, params.custom_payload_size);
            rate_tracker_update(rate_lines_snapshot, rate_last_check_ms);
        }
        if (g_sender_stop) break;

        // Pause phase
        uint32_t pause_start = millis();
        while ((millis() - pause_start) < params.pause_ms && !g_sender_stop) {
            while (g_sender_paused && !g_sender_stop) {
                vTaskDelay(10);
                rate_tracker_update(rate_lines_snapshot, rate_last_check_ms);
            }
            rate_tracker_update(rate_lines_snapshot, rate_last_check_ms);
            vTaskDelay(10);
        }
        if (g_sender_stop) break;

        rate_tracker_update(rate_lines_snapshot, rate_last_check_ms);

        // Duration check
        if (params.duration_sec > 0) {
            uint32_t elapsed = (millis() / 1000) - start_sec;
            stats_set_elapsed_sec(elapsed);
            if (elapsed >= params.duration_sec) break;
            stats_set_remaining_sec(params.duration_sec - elapsed);
        } else {
            stats_set_elapsed_sec((millis() / 1000) - start_sec);
        }
    }
}

void run_ramp(const ScenarioParams& params) {
    float start_rate = params.start_rate_hz;
    float end_rate = params.end_rate_hz;
    uint32_t ramp_dur = params.ramp_duration_sec;
    if (ramp_dur == 0) ramp_dur = 1;
    uint32_t start_sec = millis() / 1000;
    int last_logged_rate = (int)start_rate;
    uint32_t rate_lines_snapshot = 0;
    uint32_t rate_last_check_ms = 0;

    stats_set_target_rate_hz(start_rate);
    rate_tracker_init(rate_lines_snapshot, rate_last_check_ms);

    int64_t next_send = esp_timer_get_time();

    while (!g_sender_stop) {
        while (g_sender_paused && !g_sender_stop) {
            vTaskDelay(10);
            rate_tracker_update(rate_lines_snapshot, rate_last_check_ms);
        }
        if (g_sender_stop) break;

        uint32_t elapsed = (millis() / 1000) - start_sec;

        float current_rate;
        if (elapsed >= ramp_dur) {
            current_rate = end_rate;
        } else {
            current_rate = start_rate + (end_rate - start_rate) * ((float)elapsed / (float)ramp_dur);
        }
        if (current_rate < 0.1f) current_rate = 0.1f;

        int current_rate_int = (int)current_rate;
        if (current_rate_int != last_logged_rate) {
            sender_log("RATE_CHANGE from=%d to=%d Hz",
                       last_logged_rate, current_rate_int);
            last_logged_rate = current_rate_int;
        }
        stats_set_target_rate_hz(current_rate);

        int64_t interval_us = (int64_t)(1000000.0 / current_rate);

        if (!wait_until(next_send)) break;
        send_line(params.payload_size, params.custom_payload_size);
        rate_tracker_update(rate_lines_snapshot, rate_last_check_ms);
        next_send += interval_us;

        // Duration / hold logic
        if (elapsed >= ramp_dur) {
            if (params.hold_at_max > 0) {
                uint32_t hold_elapsed = elapsed - ramp_dur;
                stats_set_elapsed_sec(elapsed);
                stats_set_remaining_sec((hold_elapsed < params.hold_at_max) ? (params.hold_at_max - hold_elapsed) : 0);
                if (hold_elapsed >= params.hold_at_max) break;
            } else {
                stats_set_elapsed_sec(elapsed);
                stats_set_remaining_sec(0);
                break; // stop after ramp
            }
        } else {
            stats_set_elapsed_sec(elapsed);
            stats_set_remaining_sec(ramp_dur - elapsed);
        }

        int64_t now = esp_timer_get_time();
        if (next_send < now - interval_us) {
            next_send = now + interval_us;
        }
    }
}

void run_gap_inject(const ScenarioParams& params) {
    float rate_hz = params.rate_hz;
    if (rate_hz < 0.1f) rate_hz = 0.1f;
    int64_t interval_us = (int64_t)(1000000.0 / rate_hz);
    int64_t next_send = esp_timer_get_time();
    uint32_t start_sec = millis() / 1000;
    uint32_t next_gap_sec = start_sec + params.gap_every_sec;
    uint32_t rate_lines_snapshot = 0;
    uint32_t rate_last_check_ms = 0;

    stats_set_target_rate_hz(rate_hz);
    rate_tracker_init(rate_lines_snapshot, rate_last_check_ms);

    while (!g_sender_stop) {
        while (g_sender_paused && !g_sender_stop) {
            vTaskDelay(10);
            rate_tracker_update(rate_lines_snapshot, rate_last_check_ms);
        }
        if (g_sender_stop) break;

        if (!wait_until(next_send)) break;

        uint32_t now_sec = millis() / 1000;

        // Check gap injection
        if (now_sec >= next_gap_sec && params.gap_size > 0) {
            uint32_t seq_before = stats_get_current_seq();
            stats_increment_seq(params.gap_size);
            uint32_t seq_after = stats_get_current_seq();
            record_injected_gap(seq_before, seq_after, params.gap_size);
            sender_log("GAP_INJECT at seq=%lu skipped=%lu (%lu-%lu)",
                       (unsigned long)seq_before,
                       (unsigned long)params.gap_size,
                       (unsigned long)(seq_before + 1),
                       (unsigned long)(seq_after - 1));
            stats_increment_gaps_count();
            next_gap_sec = now_sec + params.gap_every_sec;
        } else {
            send_line(params.payload_size, params.custom_payload_size);
        }
        rate_tracker_update(rate_lines_snapshot, rate_last_check_ms);

        next_send += interval_us;

        if (params.duration_sec > 0) {
            uint32_t elapsed = now_sec - start_sec;
            stats_set_elapsed_sec(elapsed);
            if (elapsed >= params.duration_sec) break;
            stats_set_remaining_sec(params.duration_sec - elapsed);
        } else {
            stats_set_elapsed_sec(now_sec - start_sec);
            stats_set_remaining_sec(0xFFFFFFFF);
        }

        int64_t now_us = esp_timer_get_time();
        if (next_send < now_us - interval_us) {
            next_send = now_us + interval_us;
        }
    }
}

void run_endurance(const ScenarioParams& params) {
    float base_rate = params.base_rate_hz;
    if (base_rate < 0.1f) base_rate = 0.1f;
    int64_t base_interval_us = (int64_t)(1000000.0 / base_rate);
    uint32_t start_sec = millis() / 1000;
    uint32_t next_burst_sec = start_sec + params.burst_every_sec;
    int64_t next_send = esp_timer_get_time();
    bool in_burst = false;
    uint32_t burst_lines_sent = 0;
    float burst_rate = params.burst_rate_hz;
    if (burst_rate < 1.0f) burst_rate = 1.0f;
    int64_t burst_interval_us = (int64_t)(1000000.0 / burst_rate);
    uint32_t rate_lines_snapshot = 0;
    uint32_t rate_last_check_ms = 0;

    stats_set_target_rate_hz(base_rate);
    rate_tracker_init(rate_lines_snapshot, rate_last_check_ms);

    while (!g_sender_stop) {
        while (g_sender_paused && !g_sender_stop) {
            vTaskDelay(10);
            rate_tracker_update(rate_lines_snapshot, rate_last_check_ms);
        }
        if (g_sender_stop) break;

        uint32_t now_sec = millis() / 1000;

        // Trigger burst?
        if (!in_burst && now_sec >= next_burst_sec) {
            in_burst = true;
            burst_lines_sent = 0;
            stats_set_target_rate_hz(burst_rate);
        }

        if (!wait_until(next_send)) break;

        send_line(params.payload_size, params.custom_payload_size);
        rate_tracker_update(rate_lines_snapshot, rate_last_check_ms);

        if (in_burst) {
            burst_lines_sent++;
            if (burst_lines_sent >= params.burst_lines) {
                in_burst = false;
                next_burst_sec = now_sec + params.burst_every_sec;
                stats_set_target_rate_hz(base_rate);
            }
            next_send += burst_interval_us;
        } else {
            next_send += base_interval_us;
        }

        if (params.duration_sec > 0) {
            uint32_t elapsed = now_sec - start_sec;
            stats_set_elapsed_sec(elapsed);
            if (elapsed >= params.duration_sec) break;
            stats_set_remaining_sec(params.duration_sec - elapsed);
        } else {
            stats_set_elapsed_sec(now_sec - start_sec);
            stats_set_remaining_sec(0xFFFFFFFF);
        }

        int64_t now_us = esp_timer_get_time();
        int64_t active_interval = in_burst ? burst_interval_us : base_interval_us;
        if (next_send < now_us - active_interval) {
            next_send = now_us + active_interval;
        }
    }
}

const InjectedGap* get_injected_gaps(size_t& out_count) {
    out_count = s_gap_count;
    return s_gaps;
}

void clear_injected_gaps() {
    s_gap_count = 0;
}

void record_injected_gap(uint32_t seq_before, uint32_t seq_after, uint32_t gap_size) {
    if (s_gap_count < MAX_INJECTED_GAPS) {
        s_gaps[s_gap_count].timestamp_sec = millis() / 1000;
        s_gaps[s_gap_count].seq_before = seq_before;
        s_gaps[s_gap_count].seq_after = seq_after;
        s_gaps[s_gap_count].gap_size = gap_size;
        s_gap_count++;
    }
}

const char* payload_size_to_string(PayloadSize ps) {
    switch (ps) {
        case PAYLOAD_SHORT: return "short";
        case PAYLOAD_MEDIUM: return "medium";
        case PAYLOAD_LONG: return "long";
        case PAYLOAD_CUSTOM: return "custom";
    }
    return "medium";
}

PayloadSize string_to_payload_size(const char* str) {
    if (!str) return PAYLOAD_MEDIUM;
    if (strcmp(str, "short") == 0) return PAYLOAD_SHORT;
    if (strcmp(str, "medium") == 0) return PAYLOAD_MEDIUM;
    if (strcmp(str, "long") == 0) return PAYLOAD_LONG;
    if (strcmp(str, "custom") == 0) return PAYLOAD_CUSTOM;
    return PAYLOAD_MEDIUM;
}
