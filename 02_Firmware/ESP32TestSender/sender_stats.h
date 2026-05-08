#pragma once

#include <Arduino.h>
#include <atomic>
#include "config.h"

enum SenderState {
    SS_IDLE = 0,
    SS_RUNNING,
    SS_PAUSED,
    SS_COMPLETED,
    SS_ERROR
};

struct SenderStats {
    std::atomic<uint32_t> total_lines_sent{0};
    std::atomic<uint32_t> current_seq{0};
    std::atomic<float> actual_rate_hz{0.0f};
    std::atomic<float> target_rate_hz{0.0f};
    std::atomic<uint32_t> elapsed_sec{0};
    std::atomic<uint32_t> remaining_sec{0};
    std::atomic<uint32_t> injected_gaps_count{0};
    std::atomic<uint32_t> free_heap{0};
    std::atomic<SenderState> state{SS_IDLE};
};

// Global stats instance (defined in sender_stats.cpp)
extern SenderStats g_stats;

// Getters
SenderState stats_get_state();
uint32_t stats_get_total_lines();
uint32_t stats_get_current_seq();
float stats_get_actual_rate_hz();
float stats_get_target_rate_hz();
uint32_t stats_get_elapsed_sec();
uint32_t stats_get_remaining_sec();
uint32_t stats_get_injected_gaps_count();
uint32_t stats_get_free_heap();

// Setters
void stats_set_state(SenderState s);
void stats_set_target_rate_hz(float rate);
void stats_set_elapsed_sec(uint32_t sec);
void stats_set_remaining_sec(uint32_t sec);
void stats_set_free_heap(uint32_t heap);
void stats_increment_lines();
void stats_increment_seq(uint32_t by = 1);
void stats_increment_gaps_count();
void stats_reset_all();

// Rate calculator
void stats_update_actual_rate(uint32_t lines_in_interval, uint32_t interval_sec);
