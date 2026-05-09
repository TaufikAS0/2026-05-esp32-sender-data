#pragma once

#include <Arduino.h>

void ota_manager_init();
void ota_manager_handle();

bool ota_manager_enable();
void ota_manager_disable(const String& reason);
bool ota_manager_is_enabled();

String ota_manager_get_state();
String ota_manager_get_message();
const char* ota_manager_get_hostname();
uint16_t ota_manager_get_port();
