#include "sender_task.h"
#include "sender_stats.h"
#include "status_led.h"
#include "config.h"
#include "uart_sender.h"
#include <esp_timer.h>

volatile bool g_sender_paused = false;
volatile bool g_sender_stop = false;
volatile bool g_sender_start_requested = false;

SenderScenario g_active_scenario = SC_STEADY;
ScenarioParams g_active_params;

static TaskHandle_t s_sender_task_handle = nullptr;
static SemaphoreHandle_t s_start_sem = nullptr;

// Internal log circular buffer
static char s_log_buffer[SENDER_LOG_MAX_LINES][SENDER_LOG_LINE_SIZE];
static size_t s_log_head = 0;
static size_t s_log_count = 0;

void sender_log(const char* fmt, ...) {
    char entry[SENDER_LOG_LINE_SIZE];
    unsigned long ms = millis();
    unsigned long sec = ms / 1000;
    unsigned int ms_part = ms % 1000;
    int prefix = snprintf(entry, sizeof(entry), "[%02lu:%02lu:%02lu.%03u] ",
                          sec / 3600, (sec % 3600) / 60, sec % 60, ms_part);

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry + prefix, sizeof(entry) - prefix, fmt, args);
    va_end(args);

    size_t idx = s_log_head;
    strncpy(s_log_buffer[idx], entry, SENDER_LOG_LINE_SIZE - 1);
    s_log_buffer[idx][SENDER_LOG_LINE_SIZE - 1] = '\0';
    s_log_head = (s_log_head + 1) % SENDER_LOG_MAX_LINES;
    if (s_log_count < SENDER_LOG_MAX_LINES) s_log_count++;
}

const char* const* sender_log_get_lines(size_t& out_count) {
    out_count = s_log_count;
    return (const char* const*)s_log_buffer; // note: caller must handle circular wrap
}

size_t sender_log_get_head() {
    return s_log_head;
}

static void sender_task_loop(void* param) {
    (void)param;
    while (true) {
        // Wait for start signal
        if (xSemaphoreTake(s_start_sem, portMAX_DELAY) != pdTRUE) {
            vTaskDelay(100);
            continue;
        }

        if (!g_sender_start_requested) continue;
        g_sender_start_requested = false;

        g_sender_stop = false;
        g_sender_paused = false;

        stats_reset_all();
        clear_injected_gaps();
        stats_set_state(SS_RUNNING);
        status_led_set_pattern(LED_BLINK_FAST);

        sender_log("START scenario=%s rate=%.1f payload=%s baud=%lu duration=%lu",
                   (g_active_scenario == SC_STEADY) ? "steady" :
                   (g_active_scenario == SC_BURST) ? "burst" :
                   (g_active_scenario == SC_RAMP) ? "ramp" :
                   (g_active_scenario == SC_GAP_INJECT) ? "gap_inject" : "endurance",
                   g_active_params.rate_hz,
                   payload_size_to_string(g_active_params.payload_size),
                   (unsigned long)g_active_params.serial_baudrate,
                   (unsigned long)g_active_params.duration_sec);

        switch (g_active_scenario) {
            case SC_STEADY:
                run_steady(g_active_params);
                break;
            case SC_BURST:
                run_burst(g_active_params);
                break;
            case SC_RAMP:
                run_ramp(g_active_params);
                break;
            case SC_GAP_INJECT:
                run_gap_inject(g_active_params);
                break;
            case SC_ENDURANCE:
                run_endurance(g_active_params);
                break;
        }

        if (g_sender_stop) {
            sender_log("STOP at seq=%lu", (unsigned long)stats_get_current_seq());
            stats_set_state(SS_IDLE);
            status_led_set_pattern(LED_OFF);
        } else {
            sender_log("COMPLETE seq_final=%lu duration=%lus",
                       (unsigned long)stats_get_current_seq(),
                       (unsigned long)stats_get_elapsed_sec());
            stats_set_state(SS_COMPLETED);
            status_led_set_pattern(LED_DOUBLE_BLINK);
        }

        g_sender_stop = false;
        g_sender_paused = false;
    }
}

void sender_task_init() {
    s_start_sem = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(
        sender_task_loop,
        "SenderTask",
        SENDER_TASK_STACK_SIZE,
        nullptr,
        SENDER_TASK_PRIORITY,
        &s_sender_task_handle,
        SENDER_TASK_CORE
    );
}

void sender_task_start(SenderScenario scenario, const ScenarioParams& params) {
    if (stats_get_state() == SS_RUNNING || stats_get_state() == SS_PAUSED) {
        return; // caller should check
    }
    if (!uart_sender_is_ready()) {
        sender_log("UART not ready, start rejected");
        return;
    }
    g_active_scenario = scenario;
    g_active_params = params;
    if (!uart_sender_set_baudrate(params.serial_baudrate)) {
        sender_log("UART baud change failed to %lu", (unsigned long)params.serial_baudrate);
        return;
    }
    g_sender_start_requested = true;
    xSemaphoreGive(s_start_sem);
}

void sender_task_pause() {
    if (stats_get_state() == SS_RUNNING) {
        g_sender_paused = true;
        stats_set_state(SS_PAUSED);
        status_led_set_pattern(LED_BLINK_SLOW);
        sender_log("PAUSE at seq=%lu", (unsigned long)stats_get_current_seq());
    }
}

void sender_task_resume() {
    if (stats_get_state() == SS_PAUSED) {
        g_sender_paused = false;
        stats_set_state(SS_RUNNING);
        status_led_set_pattern(LED_BLINK_FAST);
        sender_log("RESUME at seq=%lu", (unsigned long)stats_get_current_seq());
    }
}

void sender_task_stop() {
    if (stats_get_state() == SS_RUNNING || stats_get_state() == SS_PAUSED) {
        g_sender_stop = true;
        g_sender_paused = false;
        // State will be updated by task after it exits scenario
    }
}
