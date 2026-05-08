# ESP32 Test Sender — Planning Document

## 1. Tujuan

Membangun ESP32 dedicated sender yang mengirim data serial ke ESP32 Logger (lihat `planning.md`) untuk uji kontinu 1×24 jam. Sender ini berfungsi sebagai **alat ukur yang terkalibrasi** — kita tahu persis berapa baris dikirim, maka kita bisa tahu persis berapa yang hilang di sisi logger.

Sender harus bisa dikonfigurasi **tanpa reflash** via web browser: ubah kecepatan, pola pengiriman, panjang payload, dan skenario uji — semua saat runtime, tanpa henti.

---

## 2. Hardware

### 2.1 Komponen

- ESP32 DevKit V1 (unit kedua, terpisah dari logger)
- Power supply 5V/2A regulated (BUKAN USB laptop — laptop bisa sleep)
- Kabel jumper: TX sender → RX logger (1 kabel data + 1 kabel GND)
- Opsional: LED indikator status (pakai built-in GPIO2)

### 2.2 Wiring ke Logger

```
SENDER ESP32              LOGGER ESP32
   TX2 (GPIO17)  ──────>  RX2 (GPIO16)
   GND           ──────>  GND
```

**Satu kabel data, satu kabel ground.** Tidak ada kabel lain yang diperlukan. TX sender ke RX logger, direct. Kalau sender 3.3V dan logger 3.3V (keduanya ESP32), tidak perlu level shifter.

**JANGAN sambungkan VCC/5V antar board** — masing-masing punya power supply sendiri. Hanya GND yang dishare.

### 2.3 Pin Assignment

| Function | GPIO |
|----------|------|
| UART2 TX (data output ke logger) | 17 |
| UART2 RX (unused, set -1) | - |
| Status LED | 2 (built-in) |

---

## 3. Software Stack

### 3.1 Build System

**Arduino IDE 2.x**, sama seperti logger. Satu project folder flat.

Library yang diinstall via Library Manager:
- `ESP Async WebServer` (by lacamera atau me-no-dev fork)
- `AsyncTCP`
- `ArduinoJson` v7

### 3.2 Folder Structure

```
ESP32TestSender/
├── ESP32TestSender.ino       <- main sketch
├── config.h                  <- semua #define
├── sender_task.h
├── sender_task.cpp           <- modul pengirim serial (FreeRTOS task)
├── scenarios.h
├── scenarios.cpp             <- modul definisi & eksekusi skenario
├── web_ui.h
├── web_ui.cpp                <- modul web server + API
├── sender_stats.h
├── sender_stats.cpp          <- modul statistik global
└── data/                     <- SPIFFS static files
    ├── index.html
    ├── app.js
    └── style.css
```

---

## 4. Format Data Output

**Harus match persis dengan `planning.md` section 5.3** agar logger bisa parse tanpa modifikasi.

Setiap baris yang dikirim sender:

```
<seq>,<payload>\n
```

### 4.1 Seq Number

- `uint32_t`, mulai dari 0 saat boot atau saat user klik "Reset & Start"
- Increment +1 per baris, tanpa skip, tanpa duplikat
- Overflow handling: setelah 4294967295, wrap ke 0 (tapi pada rate 50 baris/detik, ini 2.7 tahun — tidak relevan untuk tes 24 jam)
- Seq number adalah **sumber kebenaran** (ground truth). Kalau logger menunjukkan gap di seq, itu artinya logger yang loss — bukan sender yang skip.

### 4.2 Payload

Payload kontennya simulasi data sensor yang realistis. Format:

```
T:<suhu>,H:<humidity>,P:<tekanan>,V:<tegangan>,S:<status>
```

Contoh baris lengkap:

```
00042,T:25.3,H:62.1,P:1013.2,V:3.28,S:OK\n
```

Nilai sensor di-generate random dalam range realistis (bukan static) supaya bisa deteksi kalau logger corrupt sebagian baris (misal angka jadi garbled).

**Ukuran payload harus bisa dikonfigurasi** via web: short (~30 byte), medium (~80 byte), long (~200 byte). Ini penting karena panjang baris mempengaruhi perilaku buffer.

### 4.3 Payload Size Presets

| Preset | Total line length (incl seq + newline) | Payload content |
|--------|----------------------------------------|-----------------|
| `short` | ~35 byte | `T:25.3,S:OK` |
| `medium` | ~85 byte | `T:25.3,H:62.1,P:1013.2,V:3.28,S:OK` |
| `long` | ~200 byte | Medium + padding random alfanumerik sampai 200 byte |
| `custom` | User-defined (1-250 byte) | Medium + padding/trimming ke target size |

---

## 5. Skenario Uji

Ini inti dari modul sender. Setiap skenario simulasi kondisi berbeda yang mungkin bikin logger gagal. User pilih skenario via web, setting parameter, lalu klik Start.

### 5.1 Daftar Skenario

#### Scenario 1: `steady` — Rate konstan

Kirim baris dengan interval tetap. Ini baseline test.

Parameter:
- `rate_hz` (float): baris per detik. Range 0.1 - 200. Default 10.
- `payload_size`: short / medium / long / custom
- `duration_sec`: 0 = infinite sampai di-stop manual. Default 86400 (24 jam).

Behavior:
- Kirim 1 baris tiap `1000/rate_hz` ms. Pakai `micros()` untuk presisi, bukan `delay()`.
- Kalau rate > 100, pakai tight loop dengan `yield()` antar baris.

Use case: verifikasi logger bisa handle sustained load tanpa data loss.

#### Scenario 2: `burst` — Ledakan berkala

Kirim data dalam burst pendek diikuti silence. Simulasi sensor yang kirim batch data.

Parameter:
- `burst_lines`: jumlah baris per burst. Default 50.
- `burst_rate_hz`: rate DALAM burst. Default 100 (cepat).
- `pause_ms`: jeda antar burst. Default 2000 (2 detik).
- `payload_size`: short / medium / long / custom
- `duration_sec`: default 86400.

Behavior:
- Kirim `burst_lines` baris secepat `burst_rate_hz`, lalu diam `pause_ms`, repeat.
- Seq number tetap kontinu di semua burst (tidak reset per burst).

Use case: test buffer overflow di logger — burst cepat bisa fill queue, lalu silence memberi waktu drain.

#### Scenario 3: `ramp` — Naik bertahap

Mulai lambat, naikkan rate pelan-pelan sampai target. Berguna untuk cari titik patah (breaking point) logger.

Parameter:
- `start_rate_hz`: default 1.
- `end_rate_hz`: default 100.
- `ramp_duration_sec`: waktu dari start ke end rate. Default 3600 (1 jam).
- `hold_at_max`: setelah reach max rate, hold selama berapa detik. Default 0 (stop).
- `payload_size`: short / medium / long / custom

Behavior:
- Rate naik linear dari `start_rate_hz` ke `end_rate_hz` selama `ramp_duration_sec`.
- Setelah reach max: kalau `hold_at_max > 0`, lanjut kirim di max rate selama itu.
- Kalau `hold_at_max = 0`, stop setelah ramp selesai.

Use case: cari max rate yang masih zero-loss. Setelah tes, baca log di logger — cari seq number pertama yang hilang, lookup timestamp, hitung berapa Hz rate saat itu.

#### Scenario 4: `gap_inject` — Sengaja skip seq

Kirim data steady TAPI sengaja skip beberapa seq number secara periodik. Ini untuk **validasi bahwa sistem deteksi gap di logger & script analisis beneran bekerja**.

Parameter:
- `rate_hz`: default 10.
- `gap_every_sec`: interval injek gap. Default 300 (tiap 5 menit).
- `gap_size`: berapa seq yang di-skip per injeksi. Default 5.
- `payload_size`: short / medium / long / custom
- `duration_sec`: default 3600 (1 jam cukup).

Behavior:
- Kirim normal di `rate_hz`.
- Tiap `gap_every_sec` detik: loncat seq number sebanyak `gap_size` (misal dari 1000 langsung ke 1006).
- **Catat semua injected gap** di internal log sender (timestamp + seq_before + seq_after).

Use case: verifikasi alat ukur. Kalau logger nggak deteksi gap yang kita sengaja inject, ada bug di parser atau analysis script. Harus dijalankan SEBELUM tes 24 jam yang sebenarnya.

#### Scenario 5: `endurance` — Preset siap pakai 24 jam

Skenario komposit: combine steady + periodic burst untuk simulasi kondisi realistis.

Parameter:
- `base_rate_hz`: rate dasar. Default 10.
- `burst_every_sec`: interval burst. Default 600 (tiap 10 menit).
- `burst_lines`: baris per burst. Default 100.
- `burst_rate_hz`: rate saat burst. Default 50.
- `payload_size`: medium.
- `duration_sec`: 86400 (fixed 24 jam).

Behavior:
- Kirim di `base_rate_hz` secara default.
- Tiap `burst_every_sec`: sisipkan burst (kirim `burst_lines` baris di `burst_rate_hz`) lalu balik ke base rate.
- Seq tetap kontinu sepanjang tes.

Use case: tes produksi 24 jam final. Kondisi mirip dunia nyata di mana ada periode normal + lonjakan intermittent.

---

## 6. Web UI — Sender Control Panel

### 6.1 Halaman Utama

Layout satu halaman, tanpa navigasi tab:

**Section A: Status Bar (atas)**
- Sender status: `IDLE` / `RUNNING` / `PAUSED` / `COMPLETED` / `ERROR`
- Uptime sejak boot
- WiFi IP address
- Indikator warna: hijau=running, kuning=paused, abu=idle, merah=error

**Section B: Scenario Selector + Parameters (tengah)**
- Dropdown pilih skenario (steady, burst, ramp, gap_inject, endurance)
- Form parameter yang berubah dinamis sesuai skenario yang dipilih
- Setiap parameter punya label, input field, satuan, dan range hint
- Tombol: `▶ Start`, `⏸ Pause`, `⏹ Stop`, `🔄 Reset Seq` (reset seq ke 0)

**Section C: Live Stats (bawah)**
- Total lines sent (counter yang terus naik)
- Current seq number
- Current actual rate (Hz) — dihitung dari 1 detik terakhir
- Target rate (Hz)
- Elapsed time (HH:MM:SS)
- Remaining time (HH:MM:SS) — kalau duration terbatas
- Injected gaps (khusus skenario gap_inject): jumlah gap + list timestamp
- Free heap ESP32 sender

### 6.2 HTTP API

| Method | Path | Function |
|--------|------|----------|
| GET | `/` | Serve index.html dari SPIFFS |
| GET | `/app.js` | Serve dari SPIFFS |
| GET | `/style.css` | Serve dari SPIFFS |
| GET | `/api/status` | JSON: semua stats + state + config aktif |
| POST | `/api/start` | Body JSON: `{scenario, params}`. Validasi params, mulai kirim. Response 200 atau 400+error. |
| POST | `/api/pause` | Pause pengiriman (seq tidak reset) |
| POST | `/api/resume` | Lanjutkan dari seq terakhir |
| POST | `/api/stop` | Stop pengiriman. Seq tidak reset (supaya bisa lihat total). |
| POST | `/api/reset` | Reset seq ke 0, clear stats. Harus dalam state IDLE/COMPLETED. |
| GET | `/api/gaps` | JSON list semua injected gap (hanya untuk skenario gap_inject) |
| WS | `/ws` | WebSocket: push stats update tiap 1 detik |

### 6.3 API — Start Payload Contoh

```json
{
  "scenario": "steady",
  "params": {
    "rate_hz": 25,
    "payload_size": "medium",
    "duration_sec": 86400
  }
}
```

```json
{
  "scenario": "burst",
  "params": {
    "burst_lines": 50,
    "burst_rate_hz": 100,
    "pause_ms": 2000,
    "payload_size": "short",
    "duration_sec": 86400
  }
}
```

```json
{
  "scenario": "endurance",
  "params": {
    "base_rate_hz": 10,
    "burst_every_sec": 600,
    "burst_lines": 100,
    "burst_rate_hz": 50,
    "payload_size": "medium",
    "duration_sec": 86400
  }
}
```

### 6.4 Validasi Parameter (Server Side)

Setiap POST `/api/start` harus divalidasi:

| Parameter | Min | Max | Error jika dilanggar |
|-----------|-----|-----|---------------------|
| rate_hz / base_rate_hz | 0.1 | 200 | "Rate must be 0.1-200 Hz" |
| burst_rate_hz | 1 | 500 | "Burst rate must be 1-500 Hz" |
| burst_lines | 1 | 10000 | "Burst lines must be 1-10000" |
| pause_ms | 100 | 60000 | "Pause must be 100-60000 ms" |
| duration_sec | 0 | 259200 | "Duration max 72 hours" |
| gap_every_sec | 10 | 86400 | "Gap interval must be 10-86400 sec" |
| gap_size | 1 | 1000 | "Gap size must be 1-1000" |
| payload_size | enum | enum | "Invalid payload size" |

Return HTTP 400 + JSON `{"error": "<message>"}` kalau gagal validasi.

---

## 7. Configuration Parameters (`config.h`)

```cpp
// === WiFi ===
#define WIFI_SSID       "REPLACE_ME"
#define WIFI_PASSWORD   "REPLACE_ME"
#define MDNS_HOSTNAME   "sender"       // akses via http://sender.local

// === UART ===
#define UART_BAUDRATE   115200          // harus match dengan logger
#define UART_TX_PIN     17

// === Defaults (bisa di-override via web) ===
#define DEFAULT_RATE_HZ         10.0
#define DEFAULT_PAYLOAD_SIZE    "medium"
#define DEFAULT_DURATION_SEC    86400

// === Status LED ===
#define STATUS_LED_PIN  2

// === Web ===
#define WS_UPDATE_INTERVAL_MS   1000    // push stats ke browser tiap 1 detik
```

---

## 8. Arsitektur Internal

### 8.1 Dua FreeRTOS Task

**Sender Task** (Core 1, priority tinggi):
- Tight loop yang mengirim data ke Serial2 sesuai skenario aktif
- Menggunakan `esp_timer_get_time()` (resolusi microsecond) untuk timing presisi
- TIDAK sentuh WiFi, web, atau hal lain — hanya Serial2.print dan increment counter
- Pause/Resume via atomic flag `g_sender_paused`
- Stop via atomic flag `g_sender_stop`

**Main loop + Web** (Core 1 default / AsyncWebServer callback):
- Handle HTTP request, WebSocket broadcast
- Tidak mengganggu sender task karena async

### 8.2 Timing Presisi

Ini kritis. `delay()` tidak cukup akurat untuk rate tinggi. Pakai pendekatan ini:

```
target_interval_us = 1000000 / rate_hz
next_send_time = esp_timer_get_time()

loop:
    now = esp_timer_get_time()
    if now >= next_send_time:
        kirim baris
        next_send_time += target_interval_us  // schedule berikutnya
        // BUKAN next_send_time = now + interval (ini bikin drift)
    else:
        yield() atau vTaskDelay(1) kalau gap > 2ms
```

Pendekatan `next += interval` (bukan `now + interval`) memastikan rata-rata rate akurat walaupun ada jitter per baris. Kalau sender telat (misal karena WiFi interrupt), dia akan catch up di baris berikutnya.

### 8.3 State Machine

```
                  ┌──────────┐
         boot ──> │   IDLE   │ <── stop / complete
                  └────┬─────┘
                       │ start
                  ┌────▼─────┐
            ┌──── │ RUNNING  │ ────┐
            │     └────┬─────┘     │
          pause        │ done    error
            │     duration habis    │
       ┌────▼─────┐              ┌─▼───────┐
       │  PAUSED  │              │  ERROR  │
       └────┬─────┘              └─────────┘
            │ resume
       ┌────▼─────┐
       │ RUNNING  │
       └──────────┘
```

Transisi state hanya lewat API call (`/api/start`, `/api/pause`, dsb). State machine diimplementasi di `sender_task.cpp`, state disimpan sebagai `static` variable.

---

## 9. Status LED Behavior

| State | Pattern |
|-------|---------|
| IDLE | LED off |
| RUNNING | Steady ON |
| PAUSED | Slow blink 1Hz |
| ERROR | Fast blink 5Hz |
| COMPLETED | Double blink tiap 2 detik |

---

## 10. Sender Internal Log

Sender menyimpan log aktivitas di RAM (circular buffer 200 baris, bukan SD card — sender nggak perlu SD).

Setiap entry:

```
[HH:MM:SS.mmm] <event>
```

Events yang dicatat:
- `START scenario=steady rate=25 payload=medium duration=86400`
- `PAUSE at seq=125000`
- `RESUME at seq=125000`
- `STOP at seq=250000`
- `GAP_INJECT at seq=30000 skipped=5 (30001-30005)`
- `RATE_CHANGE from=10 to=15.5` (saat ramp)
- `COMPLETE seq_final=864000 duration=86400s`
- `ERROR <description>`

Log bisa diakses via GET `/api/log` → JSON array of strings. Berguna untuk post-mortem.

---

## 11. Prosedur Test Lengkap

Urutan langkah dari nol sampai hasil analisis. Setiap langkah punya checkpoint.

### Step 0 — Setup Hardware

1. Nyalakan kedua ESP32 dengan power supply terpisah
2. Sambungkan: Sender TX2 (GPIO17) → Logger RX2 (GPIO16), GND → GND
3. **JANGAN sambungkan VCC antar board**
4. Pastikan baudrate match di config.h kedua project (default 115200)

**Checkpoint:** kedua ESP32 boot tanpa error di Serial Monitor masing-masing.

### Step 1 — Validasi Koneksi (5 menit)

1. Buka browser ke `http://sender.local`
2. Pilih skenario `steady`, rate=5, payload=short, duration=60
3. Klik Start
4. Buka browser tab kedua ke `http://logger.local`
5. Lihat live view — baris harus muncul real-time, seq berurutan

**Checkpoint:** 60 detik × 5 Hz = 300 baris. Logger harus punya persis 300 baris di file CSV. Seq 0-299 tanpa gap.

### Step 2 — Validasi Deteksi Gap (10 menit)

1. Sender: pilih skenario `gap_inject`, rate=10, gap_every_sec=60, gap_size=3, duration=600
2. Start, tunggu 10 menit
3. Download CSV dari logger, jalankan `verify_log.py`

**Checkpoint:** Script harus deteksi persis `ceil(600/60) - 1 = 9` gap, masing-masing ukuran 3. Kalau script bilang gap count ≠ 9, ada bug di parser — FIX SEBELUM LANJUT.

### Step 3 — Stress Test Rate Limit (1 jam)

1. Sender: pilih skenario `ramp`, start=5, end=100, ramp_duration=3600
2. Start, tunggu 1 jam
3. Download CSV + stats dari logger

**Checkpoint:** Cari seq pertama yang hilang di log. Rate saat itu (dihitung dari timestamp) = breaking point logger. Kalau zero-loss sampai 100Hz — excellent, logger aman di rate target kamu.

### Step 4 — Endurance Test (24 jam)

1. Sender: pilih `endurance` preset. Default params. Duration=86400.
2. Start. Catat waktu mulai.
3. **JANGAN sentuh apapun selama 24 jam.** Boleh buka browser sesekali untuk pantau, tapi jangan restart/reflash apapun.
4. Setelah 24 jam, sender otomatis berhenti (state=COMPLETED).
5. Catat stats akhir sender: `seq_final`, `total_lines`
6. Download semua CSV dari logger via web
7. Download sender log via `/api/log`
8. Jalankan `verify_log.py`

**Checkpoint:** Ini yang menentukan. Kriteria PASS:
- `total_missing == 0` (zero data loss)
- `sd_errors == 0` di logger stats
- `queue_overflows == 0` di logger stats
- `free_heap` logger stabil (tidak menurun > 10% dari awal)
- Sender `seq_final` == jumlah baris di logger CSV

### Step 5 — Post-Mortem kalau FAIL

Kalau ada data loss:

1. Buka `gaps.csv` dari verify_log.py — lihat kapan gap terjadi (timestamp)
2. Cross-reference dengan `stats_*.csv` logger — lihat queue_hwm dan free_heap di waktu yang sama
3. Buka sender log (`/api/log`) — cari event di waktu yang sama
4. Kemungkinan root cause:
   - **queue_hwm tinggi + gap** → writer task terlalu lambat. Solusi: increase queue, optimize SD write.
   - **free_heap rendah + gap** → memory leak. Solusi: cari leak (biasanya di web server atau String concatenation).
   - **gap selalu di jam yang sama** → mungkin NTP re-sync atau WiFi reconnect mengganggu. Solusi: cek wifi_disconnects counter.
   - **gap acak sporadis** → mungkin SD card write latency spike. Solusi: pakai SD card lebih cepat (Class 10 / U3), atau increase flush interval.

---

## 12. Keamanan & Edge Cases

| Skenario | Behavior sender |
|----------|-----------------|
| WiFi disconnect saat RUNNING | Sender tetap kirim data serial (tidak depend on WiFi). Web UI jadi unreachable, tapi pengiriman tidak terganggu. Saat reconnect, web kembali normal. |
| User klik Start saat sudah RUNNING | Return HTTP 409 Conflict: `{"error": "Already running. Stop first."}` |
| User klik Reset saat RUNNING | Return HTTP 409: `{"error": "Stop the sender before resetting."}` |
| Rate terlalu tinggi (> baudrate capacity) | Validasi di start: rate_hz × average_line_bytes × 10 (bits/byte incl overhead) harus < UART_BAUDRATE. Kalau exceed, return 400 dengan pesan: "Rate exceeds UART capacity at current baudrate." |
| Duration = 0 (infinite) | Sender jalan sampai user klik Stop. Tidak ada timeout internal. |
| Power loss sender saat RUNNING | Logger tetap jalan, cuma berhenti terima data. Seq number akan berhenti naik. Di analisis: gap besar di akhir = sender mati, bukan logger yang loss. |
| Power loss logger saat RUNNING | Sender tetap kirim (tidak tahu logger mati). Data setelah logger mati = hilang. Stats flush interval (5 detik) membatasi kerugian di sisi logger. |

---

## 13. Phased Implementation — Sender

### Phase S1 — Basic Sender (no web, no scenarios)

- `ESP32TestSender.ino`: setup Serial + Serial2, connect WiFi
- `sender_task.cpp`: steady send di rate fixed dari config.h. Format sesuai section 4.
- Test: sender kirim, logger terima, cek di Serial Monitor logger

**Acceptance:** Seq kontinu di logger selama 5 menit pada rate 10 Hz.

### Phase S2 — Web Control Panel

- `web_ui.cpp`: AsyncWebServer, semua endpoint, WebSocket stats push
- SPIFFS files: index.html, app.js, style.css
- Start/Stop/Pause/Resume via API
- Parameter validation

**Acceptance:** Bisa start/stop dari browser. Stats update real-time. Invalid params ditolak dengan error message.

### Phase S3 — All Scenarios

- `scenarios.cpp`: implementasi semua 5 skenario
- Dynamic parameter form di frontend
- Internal log buffer

**Acceptance:** Setiap skenario jalan sesuai spec. gap_inject menghasilkan gap yang terprediksi.

### Phase S4 — Polish

- Status LED
- Rate capacity validation
- mDNS
- Edge case handling (concurrent start, reset saat running, dsb)

**Acceptance:** Semua edge case di section 12 di-handle.

---

## 14. Hubungan dengan Logger Planning

Dokumen ini adalah **companion** dari `planning.md` (logger). Kedua project independen tapi harus selaras di:

| Aspek | Sender (dokumen ini) | Logger (planning.md) |
|-------|---------------------|---------------------|
| Baudrate | `UART_BAUDRATE` di config.h | `UART_BAUDRATE` di config.h |
| Data format | Section 4 (output) | Section 5.3 (input) |
| Seq number | Generated, ground truth | Parsed, verified |
| Web address | `http://sender.local` | `http://logger.local` |
| UART pin | TX2 = GPIO17 | RX2 = GPIO16 |

Kalau ada perubahan di salah satu, update yang lain. Format data (section 4 sender = section 5.3 logger) adalah kontrak — jangan diubah tanpa update di kedua sisi.

---

## 15. Out of Scope

- SD card di sender (tidak perlu — sender tidak menyimpan log ke file)
- Bidirectional communication (sender hanya TX, tidak RX)
- Multiple logger support (1 sender → 1 logger)
- OTA update
- SSL/TLS di web server
