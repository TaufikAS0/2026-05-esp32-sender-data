# ESP32 Test Sender

Dedicated serial data sender untuk pengujian ESP32 Logger. Mengirim data serial dengan rate yang dikonfigurasi via web browser untuk uji kontinu 1×24 jam.

## Dokumen

- [Planning](04_Dokumen/sender-planning.md) — spesifikasi lengkap
- [Prompts](04_Dokumen/sender-codex-prompts.md) — daftar prompt build

## Hardware

- ESP32 DevKit V1
- TX2 (GPIO17) → Logger RX2 (GPIO16)
- GND → GND

## Firmware

Folder: `02_Firmware/ESP32TestSender/`

### Library yang dibutuhkan

- `ESP Async WebServer`
- `AsyncTCP`
- `ArduinoJson` v7

### Upload SPIFFS

Pastikan file `data/` di-upload ke SPIFFS sebelum upload sketch:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 --upload --port COMx 02_Firmware/ESP32TestSender
arduino-cli upload --fqbn esp32:esp32:esp32 --port COMx --input-dir build/esp32.esp32.esp32 02_Firmware/ESP32TestSender
```

### Konfigurasi WiFi

Edit `02_Firmware/ESP32TestSender/config.h`:

```cpp
#define WIFI_SSID       "YOUR_SSID"
#define WIFI_PASSWORD   "YOUR_PASSWORD"
```

## Web UI

Buka browser ke `http://sender.local`

## License

Proprietary — JIN Project
