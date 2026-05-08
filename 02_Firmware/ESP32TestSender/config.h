#pragma once

// === WiFi ===
#ifndef WIFI_SSID
#define WIFI_SSID       "WKT"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD   "69696969"
#endif
#define MDNS_HOSTNAME   "sender"       // akses via http://sender.local
#define WIFI_CONNECT_TIMEOUT_MS    12000UL
#define WIFI_RECONNECT_INTERVAL_MS 10000UL
#define HTTP_PORT                  80

// === UART ===
#define UART_BAUDRATE   115200          // harus match dengan logger
#define UART_TX_PIN     17
#define UART_RX_PIN     (-1)            // unused, set -1

// === Defaults (bisa di-override via web) ===
#define DEFAULT_RATE_HZ         10.0
#define DEFAULT_PAYLOAD_SIZE    PAYLOAD_MEDIUM
#define DEFAULT_DURATION_SEC    86400

// === Status LED ===
#define STATUS_LED_PIN  2

// === Web ===
#define WS_UPDATE_INTERVAL_MS   1000    // push stats ke browser tiap 1 detik

// === Timing ===
#define SENDER_TASK_STACK_SIZE  4096
#define SENDER_TASK_PRIORITY    5
#define SENDER_TASK_CORE        0

// === Internal Log ===
#define SENDER_LOG_MAX_LINES    200
#define SENDER_LOG_LINE_SIZE    128

// === Gaps ===
#define MAX_INJECTED_GAPS       500

// === Payload presets ===
enum PayloadSize {
    PAYLOAD_SHORT = 0,
    PAYLOAD_MEDIUM,
    PAYLOAD_LONG,
    PAYLOAD_CUSTOM
};
