#include "ota_manager.h"

#include <ArduinoOTA.h>

#include "config.h"
#include "wifi_manager.h"

namespace {

bool s_ota_enabled = false;
bool s_ota_configured = false;
String s_ota_state = "off";
String s_ota_message = "Arduino OTA nonaktif secara default.";

void configure_ota() {
    if (s_ota_configured) {
        return;
    }

    ArduinoOTA.setPort(OTA_PORT);
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setMdnsEnabled(true);
    ArduinoOTA.setRebootOnSuccess(true);

    ArduinoOTA.onStart([]() {
        s_ota_state = "uploading";
        s_ota_message = "Upload Arduino OTA sedang berjalan.";
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        const unsigned int percent = total == 0U ? 0U : (progress * 100U) / total;
        s_ota_state = "uploading";
        s_ota_message = String("Upload Arduino OTA: ") + percent + "%";
    });

    ArduinoOTA.onEnd([]() {
        s_ota_state = "restarting";
        s_ota_message = "Upload Arduino OTA selesai. ESP32 akan restart.";
    });

    ArduinoOTA.onError([](ota_error_t error) {
        s_ota_state = "error";
        switch (error) {
            case OTA_AUTH_ERROR:
                s_ota_message = "Arduino OTA gagal: auth error.";
                break;
            case OTA_BEGIN_ERROR:
                s_ota_message = "Arduino OTA gagal: begin error.";
                break;
            case OTA_CONNECT_ERROR:
                s_ota_message = "Arduino OTA gagal: connect error.";
                break;
            case OTA_RECEIVE_ERROR:
                s_ota_message = "Arduino OTA gagal: receive error.";
                break;
            case OTA_END_ERROR:
                s_ota_message = "Arduino OTA gagal: end error.";
                break;
            default:
                s_ota_message = "Arduino OTA gagal: unknown error.";
                break;
        }
    });

    s_ota_configured = true;
}

} // namespace

void ota_manager_init() {
    configure_ota();
}

void ota_manager_handle() {
    if (s_ota_enabled) {
        ArduinoOTA.handle();
    }
}

bool ota_manager_enable() {
    if (s_ota_enabled) {
        return true;
    }

    if (!wifi_manager_has_ip()) {
        s_ota_state = "error";
        s_ota_message = "Arduino OTA butuh network aktif lebih dulu.";
        return false;
    }

    configure_ota();
    ArduinoOTA.begin();

    s_ota_enabled = true;
    s_ota_state = "advertising";
    s_ota_message = String("Arduino OTA aktif di ") + OTA_HOSTNAME + ".local:" + OTA_PORT;
    return true;
}

void ota_manager_disable(const String& reason) {
    if (s_ota_enabled) {
        ArduinoOTA.end();
    }

    s_ota_enabled = false;
    s_ota_state = "off";
    s_ota_message = reason;
}

bool ota_manager_is_enabled() {
    return s_ota_enabled;
}

String ota_manager_get_state() {
    return s_ota_state;
}

String ota_manager_get_message() {
    return s_ota_message;
}

const char* ota_manager_get_hostname() {
    return OTA_HOSTNAME;
}

uint16_t ota_manager_get_port() {
    return OTA_PORT;
}
