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

static unsigned long s_last_heartbeat = 0;

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

    // WiFi (non-blocking)
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    // Don't wait here — sender task will run regardless

    // mDNS
    if (MDNS.begin(MDNS_HOSTNAME)) {
        Serial.println("mDNS responder started: http://" MDNS_HOSTNAME ".local");
    } else {
        Serial.println("mDNS responder failed to start");
    }

    // SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed!");
    } else {
        Serial.println("SPIFFS mounted");
    }

    // Init modules
    sender_task_init();
    web_ui_init();

    stats_set_state(SS_IDLE);
    status_led_set_pattern(LED_OFF);

    Serial.println("Setup complete. Sender idle.");
}

void loop() {
    // WiFi reconnect (non-blocking background)
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long s_last_wifi_retry = 0;
        if (millis() - s_last_wifi_retry > 5000) {
            s_last_wifi_retry = millis();
            Serial.println("WiFi reconnecting...");
            WiFi.reconnect();
        }
    }

    // WebSocket push
    web_ui_update();

    // Status LED update
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

    delay(10);
}
