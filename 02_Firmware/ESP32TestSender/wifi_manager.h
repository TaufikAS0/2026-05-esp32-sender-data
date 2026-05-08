#pragma once

#include <Arduino.h>

void wifi_manager_init();
void wifi_manager_update();
bool wifi_manager_is_ready();
bool wifi_manager_has_ip();
bool wifi_manager_ap_active();
String wifi_manager_ip();
const char* wifi_manager_mode_label();
const char* wifi_manager_hostname();
void wifi_manager_refresh_mdns();
