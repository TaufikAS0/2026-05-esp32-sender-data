# Codex Prompts — ESP32 Test Sender

Prompt ini untuk build modul sender (lihat `sender-planning.md`). **Selalu sertakan `sender-planning.md` di context Codex** sebelum kirim prompt apapun.

Opsional tapi direkomendasikan: sertakan juga `planning.md` (logger) supaya Codex bisa cross-reference format data.

---

## Prompt S0 — Project Initialization

> Setup skeleton Arduino IDE. Pola yang sama dengan logger: flat layout, modular .h/.cpp pairs.

```
You are an ESP32 firmware developer. We are building the project specified in sender-planning.md (attached/in context). Build system is Arduino IDE 2.x. Flat folder layout — no src/, no subfolders except data/.

Task: Initialize the project with skeleton files for every module. Each module compiles but does nothing yet.

The folder must be named `ESP32TestSender` (matching the main .ino). Reference sender-planning.md section 3.2.

Generate complete content for these files:

1. `ESP32TestSender.ino` — main sketch:
   - #include all module headers
   - setup(): Serial.begin(115200) for debug USB, Serial2 init for TX output (GPIO17, baudrate from config.h), WiFi connect (non-blocking), print boot message
   - loop(): vTaskDelay(1000)

2. `config.h` — all #define per sender-planning.md section 7. Use #pragma once.

3. `sender_task.h` + `sender_task.cpp` — skeleton: `void sender_task_init(); void sender_task_start(const char* scenario_json); void sender_task_pause(); void sender_task_resume(); void sender_task_stop();` All no-ops.

4. `scenarios.h` + `scenarios.cpp` — skeleton: enum `SenderScenario { SC_STEADY, SC_BURST, SC_RAMP, SC_GAP_INJECT, SC_ENDURANCE };` and struct `ScenarioParams` with all fields from sender-planning.md section 5 (rate_hz, burst_lines, burst_rate_hz, pause_ms, start_rate_hz, end_rate_hz, ramp_duration_sec, hold_at_max, gap_every_sec, gap_size, base_rate_hz, burst_every_sec, payload_size as enum, duration_sec). Default values per spec.

5. `sender_stats.h` + `sender_stats.cpp` — skeleton: atomic counters (total_lines_sent, current_seq, actual_rate_hz placeholder), state enum `SenderState { SS_IDLE, SS_RUNNING, SS_PAUSED, SS_COMPLETED, SS_ERROR }`, getters.

6. `web_ui.h` + `web_ui.cpp` — skeleton: `void web_ui_init();`

7. `data/index.html` — placeholder: `<h1>ESP32 Test Sender</h1>`

8. `README.md` — point to sender-planning.md

Constraints:
- Arduino IDE flat layout, NOT PlatformIO
- #pragma once on all headers
- Internal state in .cpp files uses `static`
- Public API only in headers
- Must compile cleanly in Arduino IDE with "ESP32 Dev Module" board selected

Output: full contents of every file, clearly delimited by file path.
```

---

## Prompt S1 — Phase S1 (Basic Sender)

> Sender kirim data steady tanpa web control. Validasi koneksi UART sender→logger.

```
Phase S0 (skeleton) is in place. We move to Phase S1 (Basic Sender) per sender-planning.md section 13.

Task: Implement the sender task that sends serial data at a fixed rate. No web UI yet — rate is hardcoded from config.h defaults.

Files to fill in:
- `sender_task.cpp` — full implementation of steady send
- `sender_stats.cpp` — implement atomic counters

Files to modify:
- `ESP32TestSender.ino` — spawn sender task, connect WiFi (for future use), basic serial debug output

Reference sender-planning.md sections:
- Section 4 (data format — MUST match exactly, this is the contract with the logger)
- Section 8.1 (task architecture)
- Section 8.2 (timing precision — critical)

Critical requirements:

Sender task (sender_task.cpp):
- FreeRTOS task pinned to Core 1, priority high, stack 4096
- Uses `esp_timer_get_time()` for microsecond timing per section 8.2
- Timing pattern: `next_send_time += target_interval_us` (NOT `now + interval`) to prevent drift
- Each line format: `<seq>,<payload>\n` per section 4
- Payload generation:
    - "medium" preset by default: `T:<rand_float 20-35>,H:<rand_float 30-80>,P:<rand_float 990-1030>,V:<rand_float 3.0-3.6>,S:OK`
    - Random values generated with simple `random()` seeded by `esp_random()` at boot
    - Use `snprintf` to format, NOT String concatenation (memory safety)
- Send via `Serial2.print(line)` — NOT println (we include \n in the line ourselves)
- After each send: increment `g_total_lines_sent`, `g_current_seq`
- Respect `g_sender_paused` flag (atomic bool): if true, skip send but keep timing alive
- Respect `g_sender_stop` flag (atomic bool): if true, break out of loop, set state to COMPLETED
- Print stats to USB Serial every 10 seconds: seq, actual rate (lines in last 10s / 10), uptime

sender_stats.cpp:
- Define all atomic counters declared in header
- Implement getter functions
- `g_sender_state` as `std::atomic<SenderState>`

ESP32TestSender.ino:
- setup(): init both serials, seed random, call `sender_task_init()` which starts the task immediately in RUNNING state (no web trigger needed for Phase S1)
- loop(): print heartbeat to USB Serial every 60 seconds (uptime, free heap)

WiFi:
- Connect in setup using non-blocking `WiFi.begin()`. WiFi failure must NOT prevent sender from starting. Sender task does not depend on WiFi.

Acceptance criteria (sender-planning.md section 13, Phase S1):
- Compiles in Arduino IDE
- Sender outputs data on Serial2 at configured rate
- When connected to logger: seq numbers appear in logger CSV, continuous, no gaps for 5 minutes at 10 Hz
- Timing drift over 5 minutes < 1% (check: expected_lines = rate * 300, actual should be within 1%)

Output: complete contents of all modified files. Note how you handle the microsecond timer edge cases (overflow, catch-up after delay).
```

---

## Prompt S2 — Phase S2 (Web Control Panel)

> Tambah web server untuk kontrol start/stop/pause dan monitoring stats.

```
Phase S1 is validated. Sender sends data correctly at fixed rate. We move to Phase S2 (Web Control Panel) per sender-planning.md section 13.

Task: Implement the web server with full API and frontend for controlling the sender remotely.

Files to fill in:
- `web_ui.cpp` — AsyncWebServer, all endpoints per section 6.2, WebSocket

Files to modify:
- `sender_task.cpp` — add API-driven start/stop/pause/resume with ScenarioParams, remove auto-start from init
- `ESP32TestSender.ino` — init web_ui after WiFi, remove auto-start of sender (now web-triggered), add mDNS

Frontend files to create:
- `data/index.html`
- `data/app.js`
- `data/style.css`

Reference sender-planning.md sections:
- Section 6 (Web UI spec — all subsections)
- Section 7 (config for mDNS hostname)
- Section 8.3 (state machine transitions)

Critical requirements:

web_ui.cpp:
- AsyncWebServer on port 80
- All endpoints per section 6.2 table (GET and POST)
- POST `/api/start`:
    - Parse JSON body with ArduinoJson v7
    - Extract scenario name → map to SenderScenario enum
    - Extract params → fill ScenarioParams struct
    - Validate ALL params per section 6.4 table. Return 400 + error JSON on any validation failure.
    - Check current state: if RUNNING, return 409. If IDLE or COMPLETED, proceed.
    - Call `sender_task_start(scenario, params)`
    - Return 200 + `{"ok": true, "scenario": "...", "params": {...}}`
- POST `/api/pause`, `/api/resume`, `/api/stop`:
    - State-check before acting (e.g., can't pause if not RUNNING)
    - Return 409 with clear error on invalid transition
- POST `/api/reset`:
    - Only allowed in IDLE or COMPLETED state
    - Reset seq to 0, clear all stats
- GET `/api/status`:
    - Return full JSON per section 6.2: state, all stats, active scenario+params, elapsed, remaining time
- GET `/api/gaps`:
    - Return JSON array of injected gaps (only populated during gap_inject scenario)
- GET `/api/log`:
    - Return JSON array of internal log entries (sender-planning.md section 10)
- WS `/ws`:
    - Push stats JSON every WS_UPDATE_INTERVAL_MS (1 second)
    - Only push if clients connected (`ws.count() > 0`)

Rate capacity validation (section 12):
- In POST `/api/start` handler, BEFORE starting: calculate `rate_hz * avg_line_bytes * 10`. If > UART_BAUDRATE, return 400 with message per section 12.

sender_task.cpp changes:
- Remove auto-start from init. Task starts in IDLE state, waits for signal.
- Add `sender_task_start(SenderScenario scenario, ScenarioParams params)`:
    - Store params, set state to RUNNING, signal task to begin
    - Use FreeRTOS task notification or a semaphore to wake the task
- For Phase S2, only implement STEADY scenario fully. Other scenarios can be stubs that behave like steady (they'll be completed in Phase S3).

Internal log (section 10):
- Circular buffer of 200 `char[128]` strings in static memory (no heap allocation)
- Log function `sender_log(const char* fmt, ...)` with timestamp prefix
- Log START, PAUSE, RESUME, STOP, COMPLETE, ERROR events

Frontend (data/ files):
- Match section 6.1 layout exactly
- Vanilla JS only, no frameworks
- Scenario dropdown: on change, dynamically show/hide parameter fields relevant to that scenario
- Start button: POST to /api/start with JSON body from form fields
- Pause/Resume/Stop/Reset buttons: POST to respective endpoints
- Stats section: update every 1 second from WebSocket data
- Connection indicator dot (same as logger frontend pattern)
- Disable Start while RUNNING. Disable Reset while RUNNING.
- Show validation errors from server (400 responses) in a red alert bar
- Style: clean, professional. Use system font stack. Accent color #10b981 (green, differentiate from logger's blue).
- File sizes: HTML < 3KB, JS < 8KB, CSS < 3KB (SPIFFS is limited)

ESP32TestSender.ino changes:
- setup(): WiFi connect (non-blocking), mDNS register as "sender", mount SPIFFS, call web_ui_init() after WiFi, call sender_task_init() (but task starts IDLE)
- loop(): heartbeat every 60s

Acceptance criteria (sender-planning.md section 13, Phase S2):
- Open http://sender.local in browser — UI loads
- Select "steady", set rate=10, duration=60, click Start → sender begins transmitting, stats update in real-time
- Click Pause → transmission pauses, seq holds → Resume → continues
- Click Stop → stops. Click Reset → seq goes to 0.
- Try Start with invalid rate (e.g., 999) → 400 error shown in UI
- Try Start while running → 409 error shown in UI

Output: complete contents of all created/modified files.
```

---

## Prompt S3 — Phase S3 (All Scenarios)

> Implementasi semua 5 skenario.

```
Phase S2 is validated. Web control works for steady scenario. We move to Phase S3 (All Scenarios) per sender-planning.md section 13.

Task: Implement all 5 scenarios in the scenario engine. The sender task should dispatch to the correct scenario based on what was selected via the web API.

Files to fill in:
- `scenarios.cpp` — scenario execution logic

Files to modify:
- `sender_task.cpp` — dispatch to scenario functions instead of hardcoded steady logic

Reference sender-planning.md section 5 (all subsections, CRITICAL — each scenario has specific behavior).

Critical requirements:

Architecture:
- Each scenario is a function: `void run_steady(const ScenarioParams&)`, `void run_burst(...)`, etc.
- Scenario function runs in sender_task context — it owns the loop, checks pause/stop flags, manages timing
- All scenarios share the same timing infrastructure from section 8.2 (microsecond precision, drift-free)
- All scenarios use the same `send_line(seq, payload)` helper that handles Serial2 output + counter increment

Scenario implementations (must match sender-planning.md section 5 exactly):

1. `run_steady(params)`:
    - Send at `params.rate_hz` continuously
    - Already working from Phase S2; move logic to scenarios.cpp

2. `run_burst(params)`:
    - Send `burst_lines` lines at `burst_rate_hz`, then sleep `pause_ms`, repeat
    - Seq number is CONTINUOUS across bursts (no reset per burst)
    - During pause: still check stop/pause flags, yield properly
    - Log each burst start to internal log

3. `run_ramp(params)`:
    - Rate interpolation: at elapsed time T (seconds), rate = start_rate + (end_rate - start_rate) * (T / ramp_duration)
    - Recalculate target_interval_us every 100ms or every N lines (whichever is practical)
    - After reaching end_rate: if hold_at_max > 0, continue at end_rate for that many seconds
    - Log rate changes (log every time rate crosses a whole number, e.g., 10→11→12)

4. `run_gap_inject(params)`:
    - Send steady at `rate_hz`
    - Every `gap_every_sec` seconds: skip `gap_size` seq numbers (increment seq by gap_size+1 instead of 1)
    - Record each injected gap: `{timestamp, seq_before, seq_after, gap_size}` in a static array (max 500 entries)
    - Expose gap array via `get_injected_gaps()` for the web API `/api/gaps`
    - Log each injection to internal log

5. `run_endurance(params)`:
    - Base behavior: send at `base_rate_hz`
    - Every `burst_every_sec` seconds: switch to `burst_rate_hz` for `burst_lines` lines, then back to base
    - Implementation: track time since last burst, trigger burst inline
    - Seq continuous throughout
    - Log each burst event

Frontend update (data/app.js):
- Dynamic parameter form: when user selects a scenario from dropdown, show ONLY the parameters relevant to that scenario (hide others)
- Parameter mapping per scenario:
    - steady: rate_hz, payload_size, duration_sec
    - burst: burst_lines, burst_rate_hz, pause_ms, payload_size, duration_sec
    - ramp: start_rate_hz, end_rate_hz, ramp_duration_sec, hold_at_max, payload_size
    - gap_inject: rate_hz, gap_every_sec, gap_size, payload_size, duration_sec
    - endurance: base_rate_hz, burst_every_sec, burst_lines, burst_rate_hz, payload_size, duration_sec
- For gap_inject: show "Injected Gaps" counter in stats, link to `/api/gaps` for full list

Acceptance criteria (sender-planning.md section 13, Phase S3):
- Each scenario runs per spec when started from web UI
- burst: observable pattern on logger (burst of fast data, then silence, repeat)
- ramp: rate visibly increases over time (check in sender stats)
- gap_inject: after 10 min run, `/api/gaps` returns list of gaps, count matches expected (ceil(duration/gap_every_sec) - 1). Logger analysis script detects exactly these gaps.
- endurance: base rate + periodic bursts observable in logger timestamp deltas
- All scenarios: seq is always continuous (except gap_inject intentional gaps)

Output: complete contents of all modified files.
```

---

## Prompt S4 — Phase S4 (Polish)

> Edge cases, LED, final hardening.

```
Phase S3 is validated. All scenarios work. We move to Phase S4 (Polish) per sender-planning.md section 13.

Task: Add status LED, rate capacity validation, and handle all edge cases from sender-planning.md section 12.

Files to modify:
- `ESP32TestSender.ino` — status LED init
- `web_ui.cpp` — add rate capacity check in /api/start, tighten state transition checks
- `sender_task.cpp` — LED state updates
- `sender_stats.cpp` — add elapsed/remaining time calculation

New file:
- (Optional) if LED logic is complex enough to warrant its own module, create `status_led.h` + `status_led.cpp`. Otherwise inline in sender_task.

Reference sender-planning.md sections:
- Section 9 (LED behavior)
- Section 12 (edge cases table — EVERY row must be handled)

Critical requirements:

Status LED (section 9):
- IDLE: off
- RUNNING: steady on
- PAUSED: 1Hz blink
- ERROR: 5Hz blink
- COMPLETED: double blink every 2 sec
- Implement as non-blocking using millis() checks, NOT delay()

Rate capacity validation (section 12):
- Before starting any scenario: estimate average line size for selected payload_size
- Calculate: max_rate = UART_BAUDRATE / (avg_line_bytes * 10)
- If requested rate > max_rate * 0.9 (90% safety margin): return 400 with message
- For burst scenario: check burst_rate_hz, not base rate
- For ramp: check end_rate_hz

State transition hardening (section 12):
- Start while RUNNING → 409
- Reset while RUNNING → 409
- Pause while not RUNNING → 409
- Resume while not PAUSED → 409
- Stop while IDLE → 409 (or just ignore gracefully with 200)

WiFi resilience:
- WiFi disconnect while RUNNING → sender continues. Log event. Reconnect in background.
- Web server becomes unreachable during disconnect, but sender task is unaffected.

Elapsed/remaining time:
- Track start timestamp (millis at start)
- Elapsed = (now - start) / 1000
- Remaining = max(0, duration_sec - elapsed). If duration_sec == 0 (infinite): remaining = "∞"
- Include in /api/status and WebSocket push

Acceptance criteria (sender-planning.md section 13, Phase S4):
- LED behavior matches section 9 for all states
- Rate exceeding UART capacity → rejected with clear error
- All edge cases in section 12 handled (test each one manually)
- WiFi disconnect during send: sender continues, reconnects, web resumes

Output: complete contents of all modified/created files.
```

---

## Prompt Bonus — Sender Code Review

> Pakai setelah setiap fase untuk self-check.

```
You just generated code for Phase S<N> of the ESP32 Test Sender (sender-planning.md attached).

Perform a critical code review focusing on:

1. **Timing accuracy** — is the microsecond timer used correctly per section 8.2? Any drift risk?
2. **Serial buffer** — could Serial2.print() block if logger isn't reading fast enough? (ESP32 UART TX buffer is 128 bytes by default — at high rates, print() can block)
3. **State machine integrity** — any state where flags could race (e.g., web callback sets stop while task is mid-send)?
4. **Memory** — any heap allocation in hot loop? String concatenation? snprintf buffer sizes adequate?
5. **Format compliance** — does output exactly match sender-planning.md section 4 AND planning.md section 5.3?
6. **Atomic correctness** — are shared variables between web callbacks and sender task properly atomic?
7. **Edge cases** — what happens at rate=200Hz with long payload? Does Serial2 TX buffer become bottleneck?

For each issue: severity, location, description, fix.
Then provide corrected code for critical/important issues only.
```

---

## Prompt Bonus — Integration Test Checklist Generator

> Setelah kedua modul (sender + logger) selesai. Generate checklist yang bisa di-print.

```
Both the ESP32 Logger (planning.md) and ESP32 Test Sender (sender-planning.md) are fully implemented.

Task: Generate a printable integration test checklist following sender-planning.md section 11 (full test procedure). Format as a markdown checklist with:

- Each step as a checkbox `- [ ]`
- Expected result after each step
- Space for actual result / notes
- Timestamp field for when each step was completed
- Section headers matching the step numbers (Step 0 through Step 5)
- At the bottom: summary table with columns: Test | Expected | Actual | Pass/Fail

Include the specific numbers from the planning docs (e.g., "60 sec × 5 Hz = 300 lines expected").

Output: a single markdown file `test-checklist.md` ready to print or fill in digitally.
```
