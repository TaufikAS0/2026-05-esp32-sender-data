#pragma once

#include <Arduino.h>

void web_ui_init();
void web_ui_update(); // call from loop() to broadcast WS
