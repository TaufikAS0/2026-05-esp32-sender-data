#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include "config.h"
#include "sender_task.h"
#include "sender_stats.h"
#include "web_ui.h"
#include "status_led.h"
#include "firmware_version.h"
#include "wifi_manager.h"

static unsigned long s_last_heartbeat = 0;
static bool s_spiffs_ready = false;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("========================================");
    Serial.println(FIRMWARE_NAME " v" FIRMWARE_VERSION_STRING);
    Serial.println("========================================");

    // Seed random
    randomSeed(esp_random());

    // Status LED
    status_led_init();

    // UART2 for sender output
    Serial2.begin(UART_BAUDRATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    Serial.println("Serial2 initialized at " + String(UART_BAUDRATE) + " baud");

    // SPIFFS (don't block boot if it fails)
    s_spiffs_ready = SPIFFS.begin(true);
    Serial.printf("[SPIFFS] %s\n", s_spiffs_ready ? "Mounted" : "Mount failed");

    // Init modules
    sender_task_init();
    wifi_manager_init();
    web_ui_init();

    stats_set_state(SS_IDLE);
    status_led_set_pattern(LED_OFF);

    Serial.println("Setup complete. Sender idle.");
}

void loop() {
    wifi_manager_update();
    web_ui_update();
    status_led_update();

    // Heartbeat every 60s
    if (millis() - s_last_heartbeat >= 60000) {
        s_last_heartbeat = millis();
        Serial.print("[HB] Uptime=");
        Serial.print(millis() / 1000);
        Serial.print("s FreeHeap=");
        Serial.print(ESP.getFreeHeap());
        Serial.print(" State=");
        switch (stats_get_state()) {
            case SS_IDLE: Serial.println("IDLE"); break;
            case SS_RUNNING: Serial.println("RUNNING"); break;
            case SS_PAUSED: Serial.println("PAUSED"); break;
            case SS_COMPLETED: Serial.println("COMPLETED"); break;
            case SS_ERROR: Serial.println("ERROR"); break;
        }
    }

    delay(2);
}
