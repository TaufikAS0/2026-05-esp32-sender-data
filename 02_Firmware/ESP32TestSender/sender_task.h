#pragma once

#include <Arduino.h>
#include "scenarios.h"
#include "config.h"

// Global control flags (used by scenario runners and web UI)
extern volatile bool g_sender_paused;
extern volatile bool g_sender_stop;
extern volatile bool g_sender_start_requested;

// Active scenario and params
extern SenderScenario g_active_scenario;
extern ScenarioParams g_active_params;

void sender_task_init();
void sender_task_start(SenderScenario scenario, const ScenarioParams& params);
void sender_task_pause();
void sender_task_resume();
void sender_task_stop();

// Internal log
void sender_log(const char* fmt, ...);
const char* const* sender_log_get_lines(size_t& out_count);
size_t sender_log_get_head();
