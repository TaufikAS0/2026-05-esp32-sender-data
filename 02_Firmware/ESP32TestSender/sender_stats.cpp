#include "sender_stats.h"

SenderStats g_stats;

static uint32_t s_start_millis = 0;

SenderState stats_get_state() {
    return g_stats.state.load();
}

uint32_t stats_get_total_lines() {
    return g_stats.total_lines_sent.load();
}

uint32_t stats_get_current_seq() {
    return g_stats.current_seq.load();
}

float stats_get_actual_rate_hz() {
    return g_stats.actual_rate_hz.load();
}

float stats_get_target_rate_hz() {
    return g_stats.target_rate_hz.load();
}

uint32_t stats_get_elapsed_sec() {
    if (s_start_millis == 0) return 0;
    return (millis() - s_start_millis) / 1000;
}

uint32_t stats_get_remaining_sec() {
    return g_stats.remaining_sec.load();
}

uint32_t stats_get_injected_gaps_count() {
    return g_stats.injected_gaps_count.load();
}

uint32_t stats_get_free_heap() {
    return ESP.getFreeHeap();
}

void stats_set_state(SenderState s) {
    g_stats.state.store(s);
}

void stats_set_target_rate_hz(float rate) {
    g_stats.target_rate_hz.store(rate);
}

void stats_set_elapsed_sec(uint32_t sec) {
    g_stats.elapsed_sec.store(sec);
}

void stats_set_remaining_sec(uint32_t sec) {
    g_stats.remaining_sec.store(sec);
}

void stats_set_free_heap(uint32_t heap) {
    g_stats.free_heap.store(heap);
}

void stats_increment_lines() {
    g_stats.total_lines_sent.fetch_add(1);
}

void stats_increment_seq(uint32_t by) {
    g_stats.current_seq.fetch_add(by);
}

void stats_increment_gaps_count() {
    g_stats.injected_gaps_count.fetch_add(1);
}

void stats_reset_all() {
    g_stats.total_lines_sent.store(0);
    g_stats.current_seq.store(0);
    g_stats.actual_rate_hz.store(0.0f);
    g_stats.target_rate_hz.store(0.0f);
    g_stats.elapsed_sec.store(0);
    g_stats.remaining_sec.store(0);
    g_stats.injected_gaps_count.store(0);
    s_start_millis = millis();
}

void stats_update_actual_rate(uint32_t lines_in_interval, uint32_t interval_sec) {
    if (interval_sec == 0) return;
    g_stats.actual_rate_hz.store((float)lines_in_interval / (float)interval_sec);
}
