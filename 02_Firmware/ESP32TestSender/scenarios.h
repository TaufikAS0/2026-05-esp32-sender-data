#pragma once

#include <Arduino.h>
#include "config.h"

enum SenderScenario {
    SC_STEADY = 0,
    SC_BURST,
    SC_RAMP,
    SC_GAP_INJECT,
    SC_ENDURANCE
};

struct ScenarioParams {
    // Common
    PayloadSize payload_size = DEFAULT_PAYLOAD_SIZE;
    uint32_t duration_sec = DEFAULT_DURATION_SEC;
    uint32_t custom_payload_size = 100;

    // steady, gap_inject
    float rate_hz = DEFAULT_RATE_HZ;

    // burst, endurance
    uint32_t burst_lines = 50;
    float burst_rate_hz = 100.0f;
    uint32_t pause_ms = 2000;

    // ramp
    float start_rate_hz = 1.0f;
    float end_rate_hz = 100.0f;
    uint32_t ramp_duration_sec = 3600;
    uint32_t hold_at_max = 0;

    // gap_inject
    uint32_t gap_every_sec = 300;
    uint32_t gap_size = 5;

    // endurance
    float base_rate_hz = 10.0f;
    uint32_t burst_every_sec = 600;
};

struct InjectedGap {
    uint32_t timestamp_sec;
    uint32_t seq_before;
    uint32_t seq_after;
    uint32_t gap_size;
};

// Scenario execution entry points
void scenario_init();
void run_steady(const ScenarioParams& params);
void run_burst(const ScenarioParams& params);
void run_ramp(const ScenarioParams& params);
void run_gap_inject(const ScenarioParams& params);
void run_endurance(const ScenarioParams& params);

// Helpers
void send_line(PayloadSize psize, uint32_t custom_target_size = 0);
float generate_random_float(float min_val, float max_val);

// Injected gaps
const InjectedGap* get_injected_gaps(size_t& out_count);
void clear_injected_gaps();
void record_injected_gap(uint32_t seq_before, uint32_t seq_after, uint32_t gap_size);

// Payload size helpers
const char* payload_size_to_string(PayloadSize ps);
PayloadSize string_to_payload_size(const char* str);
