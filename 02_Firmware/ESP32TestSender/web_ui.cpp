#include "web_ui.h"
#include "config.h"
#include "sender_stats.h"
#include "sender_task.h"
#include "scenarios.h"
#include "status_led.h"
#include "firmware_version.h"
#include "wifi_manager.h"
#include "ota_manager.h"
#include "uart_sender.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

static WebServer s_server(HTTP_PORT);

static const char kFallbackIndexHtml[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 Test Sender</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Oxygen,Ubuntu,Cantarell,"Open Sans","Helvetica Neue",sans-serif;background:#0f172a;color:#e2e8f0;line-height:1.5}
.container{max-width:900px;margin:0 auto;padding:16px}
header{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px}
h1{font-size:1.4rem;color:#f8fafc}
.conn{display:flex;align-items:center;gap:6px;font-size:.85rem;color:#94a3b8}
#conn-dot{width:10px;height:10px;border-radius:50%;background:#ef4444}
#conn-dot.on{background:#10b981}
.status-bar{background:#1e293b;border-radius:8px;padding:12px;margin-bottom:16px}
.stat-row{display:flex;gap:16px;flex-wrap:wrap}
.stat{display:flex;flex-direction:column;min-width:120px}
.stat .label{font-size:.75rem;color:#94a3b8;text-transform:uppercase;letter-spacing:.02em}
.stat .val{font-size:1rem;color:#f1f5f9;font-weight:600}
.badge{display:inline-block;padding:2px 8px;border-radius:999px;font-size:.8rem;text-transform:uppercase;font-weight:700}
.badge.idle{background:#475569;color:#e2e8f0}
.badge.running{background:#10b981;color:#fff}
.badge.paused{background:#f59e0b;color:#fff}
.badge.completed{background:#3b82f6;color:#fff}
.badge.error{background:#ef4444;color:#fff}
.controls{background:#1e293b;border-radius:8px;padding:16px;margin-bottom:16px}
.controls label{display:block;margin:12px 0 4px;font-size:.85rem;color:#cbd5e1}
select,input[type="number"]{width:100%;padding:8px 10px;border-radius:6px;border:1px solid #334155;background:#0f172a;color:#e2e8f0;font-size:.95rem}
select:focus,input:focus{outline:none;border-color:#10b981}
.btn-row{display:flex;gap:8px;margin-top:16px;flex-wrap:wrap}
.btn{padding:8px 14px;border-radius:6px;border:1px solid #334155;background:#334155;color:#f1f5f9;font-size:.9rem;cursor:pointer}
.btn:disabled{opacity:.5;cursor:not-allowed}
.btn.primary{background:#10b981;border-color:#10b981;color:#fff}
.btn.danger{background:#ef4444;border-color:#ef4444;color:#fff}
.ota-panel{margin-top:16px;padding:12px;border:1px solid #334155;border-radius:8px;background:#0f172a}
.ota-row{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap}
.ota-title{font-size:.75rem;color:#94a3b8;text-transform:uppercase;letter-spacing:.02em}
.ota-state{font-size:1rem;color:#f8fafc;font-weight:700}
.ota-msg{margin-top:8px;font-size:.85rem;color:#cbd5e1}
.alert{margin-top:12px;padding:8px 12px;border-radius:6px;font-size:.9rem}
.alert.err{background:#450a0a;color:#fca5a5}
.alert.ok{background:#064e3b;color:#a7f3d0}
.alert.hidden{display:none}
.live-stats{background:#1e293b;border-radius:8px;padding:16px}
.live-stats h2{font-size:1rem;margin-bottom:12px;color:#f8fafc}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:12px}
.card{background:#0f172a;border-radius:6px;padding:12px;text-align:center}
.card .label{font-size:.75rem;color:#94a3b8;margin-bottom:4px}
.card .big{font-size:1.4rem;font-weight:700;color:#f8fafc}
.notice{margin-top:16px;padding:12px;border-radius:8px;background:rgba(245,158,11,.1);border:1px solid rgba(245,158,11,.3);color:#fcd34d}
</style>
</head>
<body>
<div class="container">
  <header>
    <h1>ESP32 Test Sender <small id="fw-version" style="font-size:.65rem;color:#94a3b8;font-weight:400;">v)HTML"
FIRMWARE_VERSION_STRING
R"HTML(</small></h1>
    <div class="conn"><span id="conn-dot"></span><span id="conn-text">Connecting...</span></div>
  </header>
  <div class="notice">
    <strong>Fallback UI aktif.</strong> Upload folder <code>data/</code> ke SPIFFS untuk tampilan penuh.
  </div>
  <section class="status-bar">
    <div class="stat-row">
      <div class="stat"><span class="label">Status</span><span id="st-state" class="val badge">IDLE</span></div>
      <div class="stat"><span class="label">Uptime</span><span id="st-uptime" class="val">0s</span></div>
      <div class="stat"><span class="label">IP</span><span id="st-ip" class="val">-</span></div>
      <div class="stat"><span class="label">UART Baud</span><span id="st-baud" class="val">115200</span></div>
    </div>
  </section>
  <section class="controls">
    <label>Scenario</label>
    <select id="scenario">
      <option value="steady">Steady</option>
      <option value="burst">Burst</option>
      <option value="ramp">Ramp</option>
      <option value="gap_inject">Gap Inject</option>
      <option value="endurance">Endurance</option>
    </select>
    <div id="params"></div>
    <div class="btn-row">
      <button id="btn-start" class="btn primary">&#9654; Start</button>
      <button id="btn-pause" class="btn" disabled>&#9208; Pause</button>
      <button id="btn-resume" class="btn" disabled>&#9654; Resume</button>
      <button id="btn-stop" class="btn danger" disabled>&#9632; Stop</button>
      <button id="btn-reset" class="btn">&#8634; Reset Seq</button>
      <button id="btn-tx-test" class="btn">TX Test</button>
    </div>
    <div class="ota-panel">
      <div class="ota-row">
        <div>
          <div class="ota-title">Arduino OTA</div>
          <div id="ota-state" class="ota-state">OFF</div>
        </div>
        <button id="btn-ota" class="btn">Enable Arduino OTA</button>
      </div>
      <div id="ota-msg" class="ota-msg">Arduino OTA nonaktif secara default.</div>
    </div>
    <div id="alert" class="alert hidden"></div>
  </section>
  <section class="live-stats">
    <h2>Live Stats</h2>
    <div class="grid">
      <div class="card"><div class="label">Total Lines</div><div id="ls-lines" class="big">0</div></div>
      <div class="card"><div class="label">Current Seq</div><div id="ls-seq" class="big">0</div></div>
      <div class="card"><div class="label">Actual Rate</div><div id="ls-rate" class="big">0 Hz</div></div>
      <div class="card"><div class="label">Target Rate</div><div id="ls-target" class="big">0 Hz</div></div>
      <div class="card"><div class="label">Elapsed</div><div id="ls-elapsed" class="big">00:00:00</div></div>
      <div class="card"><div class="label">Remaining</div><div id="ls-remaining" class="big">-</div></div>
      <div class="card"><div class="label">Injected Gaps</div><div id="ls-gaps" class="big">0</div></div>
      <div class="card"><div class="label">Free Heap</div><div id="ls-heap" class="big">0</div></div>
      <div class="card"><div class="label">Scenario</div><div id="ls-scn" class="big">-</div></div>
    </div>
  </section>
</div>
<script>
(function(){
const $=id=>document.getElementById(id);
const POLL_IDLE_MS=4000;
const POLL_ACTIVE_MS=1500;
let pollTimer=null;
let pollInFlight=false;
const scenarios={
  steady:[{k:'rate_hz',l:'Rate',u:'Hz',d:10,min:0.1,max:200},{k:'serial_baudrate',l:'Serial Baud',u:'bps',d:115200,min:1200,max:921600},{k:'payload_size',l:'Payload',u:'',d:'medium',opts:['short','medium','long','counting','custom']},{k:'custom_payload_size',l:'Custom Size',u:'bytes (1-250)',d:100,min:1,max:250,showIf:{k:'payload_size',v:'custom'}},{k:'duration_sec',l:'Duration',u:'sec (0=inf)',d:86400,min:0,max:259200}],
  burst:[{k:'burst_lines',l:'Burst Lines',u:'',d:50,min:1,max:10000},{k:'burst_rate_hz',l:'Burst Rate',u:'Hz',d:100,min:1,max:500},{k:'pause_ms',l:'Pause',u:'ms',d:2000,min:100,max:60000},{k:'serial_baudrate',l:'Serial Baud',u:'bps',d:115200,min:1200,max:921600},{k:'payload_size',l:'Payload',u:'',d:'medium',opts:['short','medium','long','counting','custom']},{k:'custom_payload_size',l:'Custom Size',u:'bytes (1-250)',d:100,min:1,max:250,showIf:{k:'payload_size',v:'custom'}},{k:'duration_sec',l:'Duration',u:'sec (0=inf)',d:86400,min:0,max:259200}],
  ramp:[{k:'start_rate_hz',l:'Start Rate',u:'Hz',d:1,min:0.1,max:200},{k:'end_rate_hz',l:'End Rate',u:'Hz',d:100,min:0.1,max:200},{k:'ramp_duration_sec',l:'Ramp Duration',u:'sec',d:3600,min:1,max:259200},{k:'hold_at_max',l:'Hold at Max',u:'sec (0=stop)',d:0,min:0,max:259200},{k:'serial_baudrate',l:'Serial Baud',u:'bps',d:115200,min:1200,max:921600},{k:'payload_size',l:'Payload',u:'',d:'medium',opts:['short','medium','long','counting','custom']},{k:'custom_payload_size',l:'Custom Size',u:'bytes (1-250)',d:100,min:1,max:250,showIf:{k:'payload_size',v:'custom'}}],
  gap_inject:[{k:'rate_hz',l:'Rate',u:'Hz',d:10,min:0.1,max:200},{k:'gap_every_sec',l:'Gap Every',u:'sec',d:300,min:10,max:86400},{k:'gap_size',l:'Gap Size',u:'lines',d:5,min:1,max:1000},{k:'serial_baudrate',l:'Serial Baud',u:'bps',d:115200,min:1200,max:921600},{k:'payload_size',l:'Payload',u:'',d:'medium',opts:['short','medium','long','counting','custom']},{k:'custom_payload_size',l:'Custom Size',u:'bytes (1-250)',d:100,min:1,max:250,showIf:{k:'payload_size',v:'custom'}},{k:'duration_sec',l:'Duration',u:'sec (0=inf)',d:3600,min:0,max:259200}],
  endurance:[{k:'base_rate_hz',l:'Base Rate',u:'Hz',d:10,min:0.1,max:200},{k:'burst_every_sec',l:'Burst Every',u:'sec',d:600,min:1,max:86400},{k:'burst_lines',l:'Burst Lines',u:'',d:100,min:1,max:10000},{k:'burst_rate_hz',l:'Burst Rate',u:'Hz',d:50,min:1,max:500},{k:'serial_baudrate',l:'Serial Baud',u:'bps',d:115200,min:1200,max:921600},{k:'payload_size',l:'Payload',u:'',d:'medium',opts:['short','medium','long','counting','custom']},{k:'custom_payload_size',l:'Custom Size',u:'bytes (1-250)',d:100,min:1,max:250,showIf:{k:'payload_size',v:'custom'}},{k:'duration_sec',l:'Duration',u:'sec (0=inf)',d:86400,min:0,max:259200}]
};
function runTxTest(){
  const baud=parseInt(($('st-baud')&&$('st-baud').textContent)||'115200',10);
  return api('/tx-test',{lines:20,baudrate:isNaN(baud)?115200:baud});
}
function renderParams(){
  const sc=$('scenario').value;
  const defs=scenarios[sc];
  let html='';
  defs.forEach(p=>{
    html+=`<div class="field"><label>${p.l} <small>${p.u}</small></label>`;
    if(p.opts){html+=`<select id="p-${p.k}">${p.opts.map(o=>`<option value="${o}"${o===p.d?' selected':''}>${o}</option>`).join('')}</select>`;}
    else{html+=`<input type="number" id="p-${p.k}" value="${p.d}" step="any" min="${p.min}" max="${p.max}">`;}
    html+=`</div>`;
  });
  $('params').innerHTML=html;
  const psEl=$('p-payload_size');
  if(psEl){
    psEl.addEventListener('change',updateCustomSizeVisibility);
  }
  updateCustomSizeVisibility();
}
function updateCustomSizeVisibility(){
  const psEl=$('p-payload_size');
  const csEl=$('p-custom_payload_size');
  if(psEl&&csEl){
    const field=csEl.closest('.field');
    if(field){
      field.style.display=psEl.value==='custom'?'':'none';
    }
  }
}
function setConn(ok){$('conn-dot').className=ok?'on':'off';$('conn-text').textContent=ok?'Connected':'Disconnected';}
async function fetchStatus(){
  if(pollInFlight){return;}
  pollInFlight=true;
  try{
    const r=await fetch('/api/status');
    if(!r.ok)throw new Error('HTTP '+r.status);
    const d=await r.json();
    updateStats(d);
    setConn(true);
    schedulePoll(d.state);
  }catch(e){
    setConn(false);
    schedulePoll();
  }finally{
    pollInFlight=false;
  }
}
function schedulePoll(state){
  if(pollTimer){clearTimeout(pollTimer);}
  const delay=(state==='RUNNING'||state==='PAUSED')?POLL_ACTIVE_MS:POLL_IDLE_MS;
  pollTimer=setTimeout(fetchStatus,delay);
}
function updateStats(d){
  const st=d.state||'IDLE';
  $('st-state').textContent=st;
  $('st-state').className='val badge '+st.toLowerCase();
  $('st-uptime').textContent=fmtTime(d.uptime_sec||0);
  $('st-ip').textContent=d.ip||'-';
  $('st-baud').textContent=d.serial_baudrate||0;
  $('ls-lines').textContent=d.total_lines_sent||0;
  $('ls-seq').textContent=d.current_seq||0;
  $('ls-rate').textContent=(d.actual_rate_hz||0).toFixed(1)+' Hz';
  $('ls-target').textContent=(d.target_rate_hz||0).toFixed(1)+' Hz';
  $('ls-elapsed').textContent=fmtTime(d.elapsed_sec||0);
  const rem=d.remaining_sec;
  $('ls-remaining').textContent=(rem===0xFFFFFFFF?'\u221e':fmtTime(rem||0));
  $('ls-gaps').textContent=d.injected_gaps_count||0;
  $('ls-heap').textContent=(d.free_heap||0).toLocaleString();
  $('ls-scn').textContent=d.active_scenario||'-';
  const otaEnabled=!!d.ota_enabled;
  const otaState=(d.ota_state||'off').toUpperCase();
  $('ota-state').textContent=otaState;
  $('ota-msg').textContent=d.ota_message||'Arduino OTA nonaktif secara default.';
  $('btn-ota').textContent=otaEnabled?'Disable Arduino OTA':'Enable Arduino OTA';
  $('btn-ota').disabled=d.ota_state==='uploading'||d.ota_state==='restarting';
  const running=st==='RUNNING';
  const paused=st==='PAUSED';
  const idle=st==='IDLE'||st==='COMPLETED'||st==='ERROR';
  $('btn-start').disabled=!(idle);
  $('btn-pause').disabled=!(running);
  $('btn-resume').disabled=!(paused);
  $('btn-stop').disabled=!(running||paused);
  $('btn-reset').disabled=!(idle);
  $('btn-tx-test').disabled=!(idle);
  $('scenario').disabled=!(idle);
}
function fmtTime(s){
  const h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sec=s%60;
  return String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(sec).padStart(2,'0');
}
function showAlert(msg,ok){
  const a=$('alert');
  a.textContent=msg;
  a.className='alert '+(ok?'ok':'err');
  setTimeout(()=>{a.className='alert hidden';},4000);
}
async function api(path,body){
  try{
    const r=await fetch('/api'+path,{method:'POST',headers:{'Content-Type':'application/json'},body:body?JSON.stringify(body):undefined});
    const j=await r.json().catch(()=>({}));
    if(!r.ok){showAlert(j.error||('HTTP '+r.status),false);return false;}
    if(j.error){showAlert(j.error,false);return false;}
    showAlert('OK',true);
    await fetchStatus();
    return true;
  }catch(e){showAlert(e.message,false);return false;}
}
function toggleOta(){
  const enabled=$('btn-ota').textContent.indexOf('Disable')===0;
  return api('/ota/arduino',{enabled:!enabled});
}
function gatherParams(){
  const sc=$('scenario').value;
  const defs=scenarios[sc];
  const params={};
  defs.forEach(p=>{
    const el=$('p-'+p.k);
    if(!el)return;
    if(p.k==='custom_payload_size'){
      const psEl=$('p-payload_size');
      if(psEl&&psEl.value!=='custom')return;
    }
    params[p.k]=p.opts?el.value:parseFloat(el.value);
  });
  return {scenario:sc,params};
}
$('scenario').addEventListener('change',renderParams);
$('btn-start').addEventListener('click',()=>api('/start',gatherParams()));
$('btn-pause').addEventListener('click',()=>api('/pause'));
$('btn-resume').addEventListener('click',()=>api('/resume'));
$('btn-stop').addEventListener('click',()=>api('/stop'));
$('btn-reset').addEventListener('click',()=>api('/reset'));
$('btn-tx-test').addEventListener('click',runTxTest);
$('btn-ota').addEventListener('click',toggleOta);
renderParams();
fetchStatus();
})();
</script>
</body>
</html>
)HTML";

static const char* state_to_string(SenderState s) {
    switch (s) {
        case SS_IDLE: return "IDLE";
        case SS_RUNNING: return "RUNNING";
        case SS_PAUSED: return "PAUSED";
        case SS_COMPLETED: return "COMPLETED";
        case SS_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

static const char* scenario_to_string(SenderScenario sc) {
    switch (sc) {
        case SC_STEADY: return "steady";
        case SC_BURST: return "burst";
        case SC_RAMP: return "ramp";
        case SC_GAP_INJECT: return "gap_inject";
        case SC_ENDURANCE: return "endurance";
    }
    return "unknown";
}

static void build_status_json(JsonDocument& doc) {
    doc["state"] = state_to_string(stats_get_state());
    doc["total_lines_sent"] = stats_get_total_lines();
    doc["current_seq"] = stats_get_current_seq();
    doc["actual_rate_hz"] = stats_get_actual_rate_hz();
    doc["target_rate_hz"] = stats_get_target_rate_hz();
    doc["elapsed_sec"] = stats_get_elapsed_sec();
    doc["remaining_sec"] = stats_get_remaining_sec();
    doc["injected_gaps_count"] = stats_get_injected_gaps_count();
    doc["free_heap"] = stats_get_free_heap();
    doc["uptime_sec"] = millis() / 1000;
    doc["ip"] = wifi_manager_ip();
    doc["version"] = FIRMWARE_VERSION_STRING;
    doc["serial_baudrate"] = uart_sender_get_baudrate();
    doc["active_scenario"] = scenario_to_string(g_active_scenario);
    doc["ota_enabled"] = ota_manager_is_enabled();
    doc["ota_state"] = ota_manager_get_state();
    doc["ota_message"] = ota_manager_get_message();
    doc["ota_hostname"] = ota_manager_get_hostname();
    doc["ota_port"] = ota_manager_get_port();
}

static bool validate_params(SenderScenario sc, const ScenarioParams& p, String& out_error) {
    auto check_rate = [&](float rate, const char* name, float minv, float maxv) -> bool {
        if (rate < minv || rate > maxv) {
            out_error = String(name) + " must be " + String(minv, 1) + "-" + String(maxv, 1) + " Hz";
            return false;
        }
        return true;
    };
    auto check_baud = [&](uint32_t baud) -> bool {
        if (baud < UART_BAUDRATE_MIN || baud > UART_BAUDRATE_MAX) {
            out_error = "Serial baud must be 1200-921600";
            return false;
        }
        return true;
    };

    switch (sc) {
        case SC_STEADY:
            if (!check_rate(p.rate_hz, "Rate", 0.1f, 200.0f)) return false;
            break;
        case SC_BURST:
            if (!check_rate(p.burst_rate_hz, "Burst rate", 1.0f, 500.0f)) return false;
            if (p.burst_lines < 1 || p.burst_lines > 10000) {
                out_error = "Burst lines must be 1-10000"; return false;
            }
            if (p.pause_ms < 100 || p.pause_ms > 60000) {
                out_error = "Pause must be 100-60000 ms"; return false;
            }
            break;
        case SC_RAMP:
            if (!check_rate(p.start_rate_hz, "Start rate", 0.1f, 200.0f)) return false;
            if (!check_rate(p.end_rate_hz, "End rate", 0.1f, 200.0f)) return false;
            if (p.ramp_duration_sec < 1 || p.ramp_duration_sec > 259200) {
                out_error = "Ramp duration max 72 hours"; return false;
            }
            break;
        case SC_GAP_INJECT:
            if (!check_rate(p.rate_hz, "Rate", 0.1f, 200.0f)) return false;
            if (p.gap_every_sec < 10 || p.gap_every_sec > 86400) {
                out_error = "Gap interval must be 10-86400 sec"; return false;
            }
            if (p.gap_size < 1 || p.gap_size > 1000) {
                out_error = "Gap size must be 1-1000"; return false;
            }
            break;
        case SC_ENDURANCE:
            if (!check_rate(p.base_rate_hz, "Base rate", 0.1f, 200.0f)) return false;
            if (!check_rate(p.burst_rate_hz, "Burst rate", 1.0f, 500.0f)) return false;
            if (p.burst_lines < 1 || p.burst_lines > 10000) {
                out_error = "Burst lines must be 1-10000"; return false;
            }
            if (p.burst_every_sec < 1 || p.burst_every_sec > 86400) {
                out_error = "Burst interval must be 1-86400 sec"; return false;
            }
            break;
    }

    if (p.duration_sec > 259200) {
        out_error = "Duration max 72 hours"; return false;
    }
    if (!check_baud(p.serial_baudrate)) return false;

    float max_rate_hz = 0;
    switch (sc) {
        case SC_STEADY: max_rate_hz = p.rate_hz; break;
        case SC_BURST: max_rate_hz = p.burst_rate_hz; break;
        case SC_RAMP: max_rate_hz = p.end_rate_hz; break;
        case SC_GAP_INJECT: max_rate_hz = p.rate_hz; break;
        case SC_ENDURANCE: max_rate_hz = p.burst_rate_hz; break;
    }
    int avg_line = 90;
    if (p.payload_size == PAYLOAD_SHORT) avg_line = 48;
    else if (p.payload_size == PAYLOAD_MEDIUM) avg_line = 96;
    else if (p.payload_size == PAYLOAD_LONG) avg_line = 216;
    else if (p.payload_size == PAYLOAD_COUNTING) avg_line = 32;
    else if (p.payload_size == PAYLOAD_CUSTOM) avg_line = (int)p.custom_payload_size + 16;
    float capacity = (float)p.serial_baudrate / (float)(avg_line * 10);
    if (max_rate_hz > capacity * 0.9f) {
        out_error = "Rate exceeds UART capacity at current baudrate";
        return false;
    }

    return true;
}

static void serve_static_or_fallback(const char* spiffs_path, const char* content_type, const char* fallback) {
    if (SPIFFS.exists(spiffs_path)) {
        File f = SPIFFS.open(spiffs_path, "r");
        s_server.streamFile(f, content_type);
        f.close();
    } else {
        s_server.send(200, content_type, fallback);
    }
}

static void handle_start() {
    if (stats_get_state() == SS_RUNNING || stats_get_state() == SS_PAUSED) {
        s_server.send(409, "application/json", "{\"error\":\"Already running. Stop first.\"}");
        return;
    }
    if (!uart_sender_is_ready()) {
        s_server.send(500, "application/json", "{\"error\":\"UART2 not ready\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, s_server.arg("plain"));
    if (err) {
        s_server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const char* scenario_name = doc["scenario"] | "steady";
    SenderScenario sc = SC_STEADY;
    if (strcmp(scenario_name, "steady") == 0) sc = SC_STEADY;
    else if (strcmp(scenario_name, "burst") == 0) sc = SC_BURST;
    else if (strcmp(scenario_name, "ramp") == 0) sc = SC_RAMP;
    else if (strcmp(scenario_name, "gap_inject") == 0) sc = SC_GAP_INJECT;
    else if (strcmp(scenario_name, "endurance") == 0) sc = SC_ENDURANCE;
    else {
        s_server.send(400, "application/json", "{\"error\":\"Invalid scenario\"}");
        return;
    }

    ScenarioParams params;
    JsonObject p = doc["params"];
    if (p) {
        params.rate_hz = p["rate_hz"] | DEFAULT_RATE_HZ;
        params.burst_lines = p["burst_lines"] | 50;
        params.burst_rate_hz = p["burst_rate_hz"] | 100.0f;
        params.pause_ms = p["pause_ms"] | 2000;
        params.start_rate_hz = p["start_rate_hz"] | 1.0f;
        params.end_rate_hz = p["end_rate_hz"] | 100.0f;
        params.ramp_duration_sec = p["ramp_duration_sec"] | 3600;
        params.hold_at_max = p["hold_at_max"] | 0;
        params.gap_every_sec = p["gap_every_sec"] | 300;
        params.gap_size = p["gap_size"] | 5;
        params.custom_payload_size = p["custom_payload_size"] | 100;
        params.serial_baudrate = p["serial_baudrate"] | UART_BAUDRATE;
        params.base_rate_hz = p["base_rate_hz"] | 10.0f;
        params.burst_every_sec = p["burst_every_sec"] | 600;
        params.duration_sec = p["duration_sec"] | DEFAULT_DURATION_SEC;
        const char* ps = p["payload_size"] | "medium";
        params.payload_size = string_to_payload_size(ps);
    }

    String error_msg;
    if (!validate_params(sc, params, error_msg)) {
        String resp = "{\"error\":\"" + error_msg + "\"}";
        s_server.send(400, "application/json", resp);
        return;
    }

    sender_task_start(sc, params);

    JsonDocument resp;
    resp["ok"] = true;
    resp["scenario"] = scenario_name;
    String resp_str;
    serializeJson(resp, resp_str);
    s_server.send(200, "application/json", resp_str);
}

static void handle_tx_test() {
    if (stats_get_state() == SS_RUNNING || stats_get_state() == SS_PAUSED) {
        s_server.send(409, "application/json", "{\"error\":\"Stop the sender before TX test.\"}");
        return;
    }
    if (!uart_sender_is_ready()) {
        s_server.send(500, "application/json", "{\"error\":\"UART2 not ready\"}");
        return;
    }

    uint32_t lines = 20;
    uint32_t baudrate = uart_sender_get_baudrate();

    const String body = s_server.arg("plain");
    if (body.length() > 0) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) {
            s_server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        lines = doc["lines"] | lines;
        baudrate = doc["baudrate"] | baudrate;
    }

    if (lines < 1 || lines > 200) {
        s_server.send(400, "application/json", "{\"error\":\"TX test lines must be 1-200\"}");
        return;
    }
    if (!uart_sender_set_baudrate(baudrate)) {
        s_server.send(400, "application/json", "{\"error\":\"Invalid baudrate\"}");
        return;
    }

    sender_log("TX_TEST lines=%lu baud=%lu",
               (unsigned long)lines,
               (unsigned long)baudrate);

    status_led_set_pattern(LED_BLINK_FAST);
    for (uint32_t i = 0; i < lines; i++) {
        char line[48];
        snprintf(line, sizeof(line), "%lu,COUNT:%lu\n",
                 (unsigned long)i,
                 (unsigned long)(i + 1U));
        const size_t line_len = strlen(line);
        const size_t written = uart_sender_write(reinterpret_cast<const uint8_t*>(line), line_len);
        if (written != line_len) {
            sender_log("TX_TEST_FAIL wrote=%u expected=%u",
                       (unsigned int)written,
                       (unsigned int)line_len);
            status_led_set_pattern(LED_OFF);
            s_server.send(500, "application/json", "{\"error\":\"UART TX test write failed\"}");
            return;
        }
    }
    uart_sender_flush();
    status_led_set_pattern(LED_OFF);

    JsonDocument resp;
    resp["ok"] = true;
    resp["lines"] = lines;
    resp["baudrate"] = baudrate;
    String resp_str;
    serializeJson(resp, resp_str);
    s_server.send(200, "application/json", resp_str);
}

static void handle_ota_toggle() {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, s_server.arg("plain"));
    if (err) {
        s_server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const bool enabled = doc["enabled"] | false;
    if (enabled) {
        if (!ota_manager_enable()) {
            String resp = String("{\"error\":\"") + ota_manager_get_message() + "\"}";
            s_server.send(400, "application/json", resp);
            return;
        }
    } else {
        ota_manager_disable("Arduino OTA dimatikan dari web.");
    }

    JsonDocument resp;
    resp["ok"] = true;
    resp["enabled"] = ota_manager_is_enabled();
    resp["state"] = ota_manager_get_state();
    resp["message"] = ota_manager_get_message();
    resp["hostname"] = ota_manager_get_hostname();
    resp["port"] = ota_manager_get_port();
    String resp_str;
    serializeJson(resp, resp_str);
    s_server.send(200, "application/json", resp_str);
}

void web_ui_init() {
    s_server.on("/", HTTP_GET, []() {
        serve_static_or_fallback("/index.html", "text/html", kFallbackIndexHtml);
    });
    s_server.on("/app.js", HTTP_GET, []() {
        if (SPIFFS.exists("/app.js")) {
            File f = SPIFFS.open("/app.js", "r");
            s_server.streamFile(f, "application/javascript");
            f.close();
        } else {
            s_server.send(404, "text/plain", "Not found");
        }
    });
    s_server.on("/style.css", HTTP_GET, []() {
        if (SPIFFS.exists("/style.css")) {
            File f = SPIFFS.open("/style.css", "r");
            s_server.streamFile(f, "text/css");
            f.close();
        } else {
            s_server.send(404, "text/plain", "Not found");
        }
    });

    s_server.on("/api/status", HTTP_GET, []() {
        JsonDocument doc;
        build_status_json(doc);
        String json;
        serializeJson(doc, json);
        s_server.send(200, "application/json", json);
    });

    s_server.on("/api/start", HTTP_POST, handle_start);
    s_server.on("/api/tx-test", HTTP_POST, handle_tx_test);
    s_server.on("/api/ota/arduino", HTTP_POST, handle_ota_toggle);

    s_server.on("/api/pause", HTTP_POST, []() {
        if (stats_get_state() != SS_RUNNING) {
            s_server.send(409, "application/json", "{\"error\":\"Not running.\"}");
            return;
        }
        sender_task_pause();
        s_server.send(200, "application/json", "{\"ok\":true}");
    });

    s_server.on("/api/resume", HTTP_POST, []() {
        if (stats_get_state() != SS_PAUSED) {
            s_server.send(409, "application/json", "{\"error\":\"Not paused.\"}");
            return;
        }
        sender_task_resume();
        s_server.send(200, "application/json", "{\"ok\":true}");
    });

    s_server.on("/api/stop", HTTP_POST, []() {
        if (stats_get_state() != SS_RUNNING && stats_get_state() != SS_PAUSED) {
            s_server.send(409, "application/json", "{\"error\":\"Not running.\"}");
            return;
        }
        sender_task_stop();
        s_server.send(200, "application/json", "{\"ok\":true}");
    });

    s_server.on("/api/reset", HTTP_POST, []() {
        if (stats_get_state() == SS_RUNNING || stats_get_state() == SS_PAUSED) {
            s_server.send(409, "application/json", "{\"error\":\"Stop the sender before resetting.\"}");
            return;
        }
        stats_reset_all();
        clear_injected_gaps();
        stats_set_state(SS_IDLE);
        status_led_set_pattern(LED_OFF);
        s_server.send(200, "application/json", "{\"ok\":true}");
    });

    s_server.on("/api/gaps", HTTP_GET, []() {
        size_t count = 0;
        const InjectedGap* gaps = get_injected_gaps(count);
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (size_t i = 0; i < count; i++) {
            JsonObject obj = arr.add<JsonObject>();
            obj["timestamp_sec"] = gaps[i].timestamp_sec;
            obj["seq_before"] = gaps[i].seq_before;
            obj["seq_after"] = gaps[i].seq_after;
            obj["gap_size"] = gaps[i].gap_size;
        }
        String json;
        serializeJson(doc, json);
        s_server.send(200, "application/json", json);
    });

    s_server.on("/api/log", HTTP_GET, []() {
        size_t count = 0;
        const char* const* lines = sender_log_get_lines(count);
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        if (count > 0) {
            size_t start = (SENDER_LOG_MAX_LINES + sender_log_get_head() - count) % SENDER_LOG_MAX_LINES;
            for (size_t i = 0; i < count; i++) {
                size_t idx = (start + i) % SENDER_LOG_MAX_LINES;
                arr.add((const char*)lines[idx]);
            }
        }
        String json;
        serializeJson(doc, json);
        s_server.send(200, "application/json", json);
    });

    s_server.begin();
    Serial.printf("[WEB] Server started on port %d\n", HTTP_PORT);
}

void web_ui_update() {
    s_server.handleClient();
}
