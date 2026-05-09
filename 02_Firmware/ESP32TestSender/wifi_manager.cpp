#include "wifi_manager.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "config.h"

namespace {

enum class NetworkMode {
  Offline,
  Station,
  AccessPoint,
  Hybrid
};

NetworkMode s_mode = NetworkMode::Offline;
bool s_mdns_started = false;
bool s_had_connection = false;
bool s_ap_active = false;
bool s_sta_attempt_active = false;
uint32_t s_last_reconnect_attempt_ms = 0U;
uint32_t s_sta_attempt_started_ms = 0U;
constexpr char kFallbackApSsid[] = "ESP-Sender-Setup";

bool start_mdns() {
  if (!wifi_manager_has_ip()) {
    return false;
  }

  if (!MDNS.begin(MDNS_HOSTNAME)) {
    Serial.printf("[MDNS] Failed to start for %s.local\n", MDNS_HOSTNAME);
    s_mdns_started = false;
    return false;
  }

  MDNS.addService("http", "tcp", HTTP_PORT);
  s_mdns_started = true;
  Serial.printf("[MDNS] %s.local ready\n", MDNS_HOSTNAME);
  return true;
}

void stop_mdns() {
  if (!s_mdns_started) {
    return;
  }
  MDNS.end();
  s_mdns_started = false;
}

void update_mode_flags() {
  const bool sta_ready = WiFi.status() == WL_CONNECTED;

  if (sta_ready && s_ap_active) {
    s_mode = NetworkMode::Hybrid;
    return;
  }

  if (sta_ready) {
    s_mode = NetworkMode::Station;
    return;
  }

  if (s_ap_active) {
    s_mode = NetworkMode::AccessPoint;
    return;
  }

  s_mode = NetworkMode::Offline;
}

void stop_fallback_ap() {
  if (!s_ap_active) {
    return;
  }
  WiFi.softAPdisconnect(true);
  s_ap_active = false;
  Serial.printf("[WIFI] Fallback AP stopped\n");
  update_mode_flags();
}

void start_fallback_ap() {
  if (s_ap_active) {
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  s_ap_active = WiFi.softAP(kFallbackApSsid);
  if (s_ap_active) {
    Serial.printf("[WIFI] Fallback AP active SSID=%s IP=%s\n",
                  kFallbackApSsid,
                  WiFi.softAPIP().toString().c_str());
  } else {
    Serial.printf("[WIFI] Failed to start fallback AP\n");
  }
  update_mode_flags();
}

void connect_station_async() {
  if (strlen(WIFI_SSID) == 0U || strcmp(WIFI_SSID, "REPLACE_ME") == 0) {
    Serial.printf("[WIFI] STA credentials not configured, using fallback AP only\n");
    start_fallback_ap();
    return;
  }

  WiFi.mode(s_ap_active ? WIFI_AP_STA : WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  s_sta_attempt_active = true;
  s_sta_attempt_started_ms = millis();
  s_last_reconnect_attempt_ms = millis();
  Serial.printf("[WIFI] Connecting to %s\n", WIFI_SSID);
}

} // namespace

void wifi_manager_init() {
  WiFi.setSleep(false);
  stop_mdns();
  s_mode = NetworkMode::Offline;
  s_had_connection = false;
  s_ap_active = false;
  s_sta_attempt_active = false;
  s_last_reconnect_attempt_ms = 0U;
  s_sta_attempt_started_ms = 0U;
  connect_station_async();
}

void wifi_manager_update() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!s_had_connection || s_sta_attempt_active || s_mode != NetworkMode::Station) {
      s_had_connection = true;
      s_sta_attempt_active = false;
      stop_fallback_ap();
      update_mode_flags();
      Serial.printf("[WIFI] Connection restored IP=%s\n", WiFi.localIP().toString().c_str());
    }
    if (!s_mdns_started) {
      start_mdns();
    }
    return;
  }

  if (s_had_connection && s_mode == NetworkMode::Station) {
    Serial.printf("[WIFI] Disconnected, waiting to reconnect\n");
    s_had_connection = false;
    stop_mdns();
  }

  if (s_sta_attempt_active && millis() - s_sta_attempt_started_ms >= WIFI_CONNECT_TIMEOUT_MS) {
    s_sta_attempt_active = false;
    Serial.printf("[WIFI] Connect timeout, enabling fallback AP\n");
    start_fallback_ap();
  }

  if (!s_sta_attempt_active && millis() - s_last_reconnect_attempt_ms >= WIFI_RECONNECT_INTERVAL_MS) {
    Serial.printf("[WIFI] Reconnect attempt\n");
    connect_station_async();
  }

  update_mode_flags();
}

bool wifi_manager_is_ready() {
  return (s_mode == NetworkMode::Station || s_mode == NetworkMode::Hybrid) && WiFi.status() == WL_CONNECTED;
}

bool wifi_manager_has_ip() {
  return wifi_manager_is_ready() || s_ap_active;
}

bool wifi_manager_ap_active() {
  return s_ap_active;
}

String wifi_manager_ip() {
  if (wifi_manager_is_ready()) {
    return WiFi.localIP().toString();
  }
  if (s_ap_active) {
    return WiFi.softAPIP().toString();
  }
  return String("offline");
}

const char* wifi_manager_mode_label() {
  switch (s_mode) {
    case NetworkMode::Hybrid:
      return "AP+STA";
    case NetworkMode::AccessPoint:
      return "AP";
    case NetworkMode::Station:
      return "STA";
    default:
      return "OFFLINE";
  }
}

const char* wifi_manager_hostname() {
  return MDNS_HOSTNAME;
}

void wifi_manager_refresh_mdns() {
  if (!wifi_manager_is_ready()) {
    return;
  }

  if (!MDNS.begin(MDNS_HOSTNAME)) {
    Serial.printf("[MDNS] Rebuild failed for %s.local\n", MDNS_HOSTNAME);
    s_mdns_started = false;
    return;
  }

  MDNS.addService("http", "tcp", HTTP_PORT);
  s_mdns_started = true;
  Serial.printf("[MDNS] HTTP service restored for %s.local\n", MDNS_HOSTNAME);
}
