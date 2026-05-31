/*
 * wifi_ui.cpp — implementation
 */
#include "wifi_ui.h"
#include "settings.h"
#include "obd2.h"
#include "lever_decoder.h"
#include "coolant.h"
#include "haldex_link.h"
#include "serial_proto.h"
#include "button_sniffer.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// =============================================================
//  State
// =============================================================
static WebServer g_server(80);
static bool     g_active   = false;
static String   g_curr_ssid = "";

// Build version is owned by serial_proto; we'll just read SETTINGS_VERSION
// from settings struct for display purposes.

// =============================================================
//  Mobile HTML (served at "/")
//  Kept short + dependency-free so it fits in PROGMEM.
//  Auto-polls /api/status every 500 ms.
// =============================================================
static const char MOBILE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="fr"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>BOT32 mobile</title>
<style>
  *{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
  body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;background:#1a1a2e;color:#e8e8f0;margin:0;padding:14px;font-size:16px}
  h1{margin:0 0 14px;font-size:22px;color:#7df;display:flex;align-items:center;gap:8px}
  h1 .v{font-size:11px;background:#0a4;padding:2px 7px;border-radius:10px;font-weight:600}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
  .card{background:#252540;border-radius:10px;padding:12px;border:1px solid #404060}
  .lbl{font-size:11px;color:#8a8aa0;text-transform:uppercase;letter-spacing:.5px}
  .val{font-size:24px;font-weight:600;margin-top:4px;color:#fff}
  .val.stale{color:#666}
  .can{font-size:10px;color:#666;font-family:monospace;margin-top:2px}
  .actions{margin-top:14px;display:grid;gap:8px}
  button{background:#3a3a5a;color:#fff;border:none;padding:14px;border-radius:10px;font-size:16px;font-weight:600;cursor:pointer;active:bg-#4a4a6a}
  button.danger{background:#a33}
  button.primary{background:#0a4}
  button:active{transform:scale(.98)}
  .status{margin-top:10px;font-size:12px;color:#7af;text-align:center;min-height:18px}
  .conn{position:fixed;top:0;right:0;padding:4px 10px;font-size:10px;background:#0a4;color:#fff;border-radius:0 0 0 8px}
  .conn.off{background:#a33}
</style></head>
<body>
<div class="conn" id="conn">●</div>
<h1>BOT32 <span class="v" id="v">—</span></h1>

<div class="grid">
  <div class="card">
    <div class="lbl">Lever</div><div class="val" id="lever">—</div>
    <div class="can">0x394 WBA_03</div>
  </div>
  <div class="card">
    <div class="lbl">MAP mbar</div><div class="val" id="map">—</div>
    <div class="can">DID 0x39C0</div>
  </div>
  <div class="card">
    <div class="lbl">Coolant TX</div><div class="val" id="cool">—</div>
    <div class="can">0x647 Motor_09</div>
  </div>
  <div class="card">
    <div class="lbl">Real coolant</div><div class="val" id="rcool">—</div>
    <div class="can">0x647 RX</div>
  </div>
  <div class="card">
    <div class="lbl">⛽ Ethanol %</div><div class="val" id="eth">—</div>
    <div class="can">DID 0xF452</div>
  </div>
  <div class="card">
    <div class="lbl">🏁 Haldex %</div><div class="val" id="hdx">—</div>
    <div class="can">DID 0x2BF3</div>
  </div>
  <div class="card">
    <div class="lbl">🌡 DSG oil °C</div><div class="val" id="dsg">—</div>
    <div class="can">DID 0x2104</div>
  </div>
  <div class="card">
    <div class="lbl">🔥 EGT °C</div><div class="val" id="egt">—</div>
    <div class="can">DID 0x40D5</div>
  </div>
  <div class="card">
    <div class="lbl">🅿 Hand brake</div><div class="val" id="hbr">—</div>
    <div class="can">0x30B b[2] bit 7</div>
  </div>
  <div class="card">
    <div class="lbl">🅾 OK button</div><div class="val" id="okb">—</div>
    <div class="can">0x5BF b[0]</div>
  </div>
  <div class="card">
    <div class="lbl">⚠ Hazard</div><div class="val" id="hz">—</div>
    <div class="can">0x366 b[2] bit 4</div>
  </div>
  <div class="card">
    <div class="lbl">🚫 TC button</div><div class="val" id="tc">—</div>
    <div class="can">0x0FD b[6]</div>
  </div>
</div>

<div class="actions">
  <button class="primary" onclick="cmd('clear_engine_fault')">🔧 Clear Engine Fault</button>
  <button onclick="location.href='/settings'">⚙ Tous les réglages</button>
</div>

<div class="status" id="st">—</div>

<script>
const $ = id => document.getElementById(id);
let lastOk = 0;

async function poll(){
  try{
    const r = await fetch('/api/status',{cache:'no-store'});
    if(!r.ok) throw 0;
    const s = await r.json();
    lastOk = Date.now();
    $('v').textContent = s.version ? 'v'+s.version : '—';
    $('lever').textContent = (s.lever && s.lever!=='?') ? (s.gear?s.lever+s.gear:s.lever) : '—';
    setVal('map', s.map_mbar>=0 ? Math.round(s.map_mbar) : null);
    setVal('cool', s.coolant_byte!==undefined ? (s.coolant_byte*0.7339-43.94).toFixed(1)+'°' : null, !s.motor09_tx_active);
    setVal('rcool', s.real_coolant_c>=0 ? s.real_coolant_c.toFixed(1)+'°' : null);
    setVal('eth', s.ethanol_pct>=0 ? s.ethanol_pct.toFixed(1)+'%' : null);
    setVal('hdx', s.haldex_blockage_pct>=0 ? s.haldex_blockage_pct.toFixed(1)+'%' : null);
    // v2.8.0 — 3 temps + 2 sniffers (sentinel -1000 = no data for temps)
    setVal('dsg',  (s.dsg_oil_c    !== undefined && s.dsg_oil_c    > -999) ? s.dsg_oil_c.toFixed(0)    + '°' : null);
    setVal('egt',  (s.egt_c        !== undefined && s.egt_c        > -999) ? s.egt_c.toFixed(0)        + '°' : null);
    const hbFresh = (s.handbrake_age_ms !== undefined && s.handbrake_age_ms < 5000);
    setVal('hbr', hbFresh ? (s.handbrake_active ? '✓ ON' : 'OFF') : null);
    const okFresh = (s.ok_button_age_ms !== undefined && s.ok_button_age_ms < 5000);
    setVal('okb', okFresh ? (s.ok_button_pressed ? '✓ PRESS' : 'rel.') : null);
    // v2.9.0 — Hazard + TC sniffers
    const hzFresh = (s.hazard_age_ms !== undefined && s.hazard_age_ms < 5000);
    setVal('hz', hzFresh ? (s.hazard_active ? '✓ ON' : 'OFF') : null);
    const tcFresh = (s.tc_button_age_ms !== undefined && s.tc_button_age_ms < 5000);
    // v2.10.0: bouton tenu = traction control OFF ; relâché = ON (normal)
    setVal('tc', tcFresh ? (s.tc_button_pressed ? 'OFF' : 'ON') : null);
    $('conn').classList.remove('off');
    $('conn').textContent='●';
  }catch(e){
    $('conn').classList.add('off');
    $('conn').textContent='✗';
  }
}
function setVal(id, v, stale){
  const el = $(id);
  if(v===null || v===undefined){ el.textContent='—'; el.classList.add('stale'); }
  else{ el.textContent=v; el.classList.toggle('stale', !!stale); }
}
async function cmd(c){
  $('st').textContent='⏳ envoi…';
  try{
    const r = await fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({cmd:c})});
    const j = await r.json();
    $('st').textContent = j.ok ? '✓ '+c+' OK' : '✗ '+c+' '+(j.msg||'fail');
    setTimeout(()=>$('st').textContent='—', 3000);
  }catch(e){ $('st').textContent='✗ erreur réseau'; }
}
setInterval(poll, 500);
poll();
</script>
</body></html>)HTML";

// =============================================================
//  Mobile SETTINGS page (served at "/settings")
//  Compact form with all main settings, mirrors PC UI in dark theme.
//  Fetches current values from /api/get_settings, POSTs changes to /api/set.
// =============================================================
static const char SETTINGS_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="fr"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>BOT32 settings</title>
<style>
  *{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
  body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;background:#1a1a2e;color:#e8e8f0;margin:0;padding:14px;font-size:15px;padding-bottom:60px}
  h1{margin:0 0 8px;font-size:20px;color:#7df}
  h2{font-size:14px;color:#7df;border-bottom:1px solid #404060;padding-bottom:4px;margin:18px 0 8px;text-transform:uppercase;letter-spacing:.5px}
  .row{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:8px}
  .row.full{grid-template-columns:1fr}
  label{display:block;font-size:11px;color:#8a8aa0;text-transform:uppercase;letter-spacing:.5px;margin-bottom:3px}
  input,select{width:100%;padding:8px;background:#252540;border:1px solid #404060;color:#fff;border-radius:6px;font-size:15px;font-family:inherit}
  input[type=checkbox]{width:22px;height:22px;vertical-align:middle;margin-right:6px}
  .cb{display:flex;align-items:center;font-size:14px;padding:6px 0;color:#e8e8f0}
  .hint{font-size:10px;color:#666;margin-top:2px}
  .save{position:fixed;bottom:0;left:0;right:0;padding:10px 14px;background:#0a4;color:#fff;text-align:center;font-weight:600;font-size:14px}
  .save.err{background:#a33}
  .back{display:inline-block;color:#7df;text-decoration:none;font-size:13px;margin-bottom:10px}
</style></head>
<body>
<a class="back" href="/">← Retour live</a>
<h1>⚙ Réglages BOT32</h1>

<h2>🎯 MAP → Gauge mapping</h2>
<div class="row">
  <div><label>MAP min mbar</label><input type="number" data-k="map_min_mbar" min="0" max="4000" step="10"></div>
  <div><label>MAP max mbar</label><input type="number" data-k="map_max_mbar" min="0" max="4000" step="10"></div>
</div>

<h2>🆔 Cluster bus IDs</h2>
<div class="row">
  <div><label>Motor_09 ID</label><input type="text" data-k="cluster_motor09_id" data-hex="1"></div>
  <div><label>WBA_03 ID</label><input type="text" data-k="cluster_wba03_id" data-hex="1"></div>
</div>

<h2>🔍 OBD2 bus IDs</h2>
<div class="row">
  <div><label>Req ID</label><input type="text" data-k="obd2_req_id" data-hex="1"></div>
  <div><label>Resp ID</label><input type="text" data-k="obd2_resp_id" data-hex="1"></div>
</div>
<div class="row">
  <div><label>DID MAP</label><input type="text" data-k="obd2_did_map" data-hex="1"></div>
  <div><label>Poll Hz</label><input type="number" data-k="obd2_poll_hz" min="1" max="30"></div>
</div>

<h2>⏱ TX</h2>
<div class="row">
  <div><label>TX rate Hz</label><input type="number" data-k="tx_rate_hz" min="5" max="50"></div>
</div>
<label class="cb"><input type="checkbox" data-k="tx_enabled"> 🟢 TX enabled (master)</label>
<label class="cb"><input type="checkbox" data-k="force_tx_always"> 🔧 Force TX in ALL modes</label>

<h2>🧪 Bench test</h2>
<label class="cb"><input type="checkbox" data-k="bench_test_enabled"> Enable bench test</label>
<div class="row">
  <div><label>Bench bus</label>
    <select data-k="bench_test_bus"><option value="0">CAN0 cluster</option><option value="1">CAN1 OBD2</option></select>
  </div>
  <div><label>Bench RPM</label><input type="number" data-k="bench_rpm" min="0" max="8000" step="50"></div>
</div>
<div class="row">
  <div><label>Bench MAP mbar</label><input type="number" data-k="bench_map_mbar" min="0" max="3000" step="20"></div>
</div>

<h2>🔧 Clear Engine Fault auto-trigger</h2>
<label class="cb"><input type="checkbox" data-k="cef_auto_enabled"> 🟢 Auto-trigger enabled</label>
<div class="row">
  <div><label>Trigger CAN ID</label><input type="text" data-k="cef_trigger_can_id" data-hex="1"></div>
  <div><label>Byte idx</label><input type="number" data-k="cef_trigger_byte_idx" min="0" max="7"></div>
</div>
<div class="row">
  <div><label>Rest value</label><input type="number" data-k="cef_trigger_rest_value" min="0" max="255"></div>
  <div><label>Pressed value</label><input type="number" data-k="cef_trigger_pressed_value" min="0" max="255"></div>
</div>
<div class="row">
  <div><label>Press count</label><input type="number" data-k="cef_press_count" min="1" max="10"></div>
  <div><label>Window ms</label><input type="number" data-k="cef_press_window_ms" min="500" max="30000" step="100"></div>
</div>

<h2>📶 WiFi AP</h2>
<label class="cb"><input type="checkbox" data-k="wifi_enabled"> 📶 WiFi AP enabled</label>
<div class="row">
  <div><label>SSID</label><input type="text" data-k="wifi_ap_ssid" maxlength="32"></div>
  <div><label>Password (8+)</label><input type="text" data-k="wifi_ap_password" maxlength="63"></div>
</div>

<h2>🏁 Haldex AWD link</h2>
<label class="cb"><input type="checkbox" data-k="haldex_enabled"> 🏁 Haldex link enabled</label>
<div class="row">
  <div><label>Bus</label>
    <select data-k="haldex_bus"><option value="0">CAN0 cluster</option><option value="1">CAN1 chassis</option></select>
  </div>
  <div><label>Transport</label>
    <select data-k="haldex_transport"><option value="0">CAN</option><option value="1">ESP-NOW</option></select>
  </div>
</div>
<div class="row">
  <div><label>State broadcast ID</label><input type="text" data-k="haldex_state_id" data-hex="1"></div>
  <div><label>Cmd ID</label><input type="text" data-k="haldex_cmd_id" data-hex="1"></div>
</div>
<div class="row full">
  <div><label>ESP-NOW peer MAC</label><input type="text" data-k="haldex_espnow_peer_mac" placeholder="FF:FF:FF:FF:FF:FF"></div>
</div>

<div class="save" id="save">Touche un champ pour modifier</div>

<script>
function toHex(n){return '0x'+Number(n||0).toString(16).toUpperCase().padStart(3,'0')}
function parseHex(s){if(!s)return 0;s=String(s).trim();if(s.startsWith('0x'))return parseInt(s.substring(2),16);if(/[a-fA-F]/.test(s))return parseInt(s,16);return parseInt(s,10)}

async function load(){
  try{
    const r = await fetch('/api/get_settings');
    const s = await r.json();
    document.querySelectorAll('[data-k]').forEach(el=>{
      const k = el.dataset.k;
      if(s[k] === undefined) return;
      if(el.type === 'checkbox'){ el.checked = !!s[k]; }
      else if(el.dataset.hex === '1'){ el.value = toHex(s[k]); }
      else if(el.tagName === 'SELECT'){ el.value = String(s[k]); }
      else { el.value = s[k]; }
    });
    setStatus('Réglages chargés', false);
  }catch(e){ setStatus('Erreur chargement', true); }
}

async function save(k, v){
  setStatus('⏳ Envoi '+k+'...', false);
  try{
    const r = await fetch('/api/set', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({key:k, value:v})});
    const j = await r.json();
    setStatus(j.ok ? '✓ '+k+' sauvegardé' : '✗ '+k+' refusé : '+(j.msg||'?'), !j.ok);
  }catch(e){ setStatus('✗ Erreur réseau', true); }
}

function setStatus(t, err){
  const el = document.getElementById('save');
  el.textContent = t;
  el.classList.toggle('err', !!err);
}

// Auto-save on change/blur
document.querySelectorAll('[data-k]').forEach(el=>{
  const ev = (el.type==='checkbox'||el.tagName==='SELECT') ? 'change' : 'blur';
  el.addEventListener(ev, ()=>{
    const k = el.dataset.k;
    let v;
    if(el.type === 'checkbox'){ v = el.checked; }
    else if(el.dataset.hex === '1'){
      v = parseHex(el.value);
      el.value = toHex(v);
    }
    else if(el.type === 'number'){ v = +el.value; }
    else { v = el.value; }

    // v2.7.1: bench mode UX — warn about jumper + auto TX toggle
    if(k === 'bench_test_enabled'){
      if(v){
        if(!confirm('Activer bench mode?\n\n1. Mets jumpers 120Ω HAT en ON\n2. TX sera auto-activé\n3. TX sera restauré au disable\n\nOK ?')){
          el.checked = false;
          return;
        }
      } else {
        if(!confirm('Désactiver bench mode?\n\n1. Remets jumpers 120Ω en OFF\n2. TX sera remis à son état précédent\n\nOK ?')){
          el.checked = true;
          return;
        }
      }
    }

    save(k, v);
  });
});

load();
</script>
</body></html>)HTML";

// =============================================================
//  Route handlers
// =============================================================
static void handle_root() {
  g_server.send_P(200, "text/html", MOBILE_HTML);
}

static void handle_status() {
  // Build the same status payload as serial_proto::emit_status, just trimmed
  // to the fields the mobile UI actually displays.
  JsonDocument doc;
  doc["version"]     = "2.10.0";   // keep in sync with BUILD_VERSION
  doc["uptime_ms"]   = millis();
  doc["lever"]       = String(lever_get());
  doc["gear"]        = lever_get_gear();
  doc["map_mbar"]    = obd2_get_last_map_mbar();
  doc["coolant_byte"] = (uint8_t)0;   // placeholder; mobile UI computes temp itself
  // For coolant: re-derive from current MAP so phone shows what BOT32 would TX
  float map = obd2_get_last_map_mbar();
  const Settings& s = settings_get();
  if (map < 0) map = s.map_min_mbar;
  doc["coolant_byte"] = coolant_map_mbar_to_byte(map, s.map_min_mbar, s.map_max_mbar);
  doc["motor09_tx_active"] = s.tx_enabled;  // simplified — mobile doesn't need full mode logic
  doc["real_coolant_c"]    = coolant_get_real_temp_c();
  doc["ethanol_pct"]       = obd2_get_last_ethanol_pct();
  doc["haldex_blockage_pct"] = obd2_get_last_haldex_blockage_pct();
  // v2.10.0 — 2 temps (DSG + EGT) + sniffer flags for mobile live grid
  doc["dsg_oil_c"]       = obd2_get_last_dsg_oil_c();
  doc["egt_c"]           = obd2_get_last_egt_c();
  // v2.10.0: engine oil temp removed from mobile status.
  doc["handbrake_active"]  = button_sniffer_handbrake_active();
  doc["handbrake_age_ms"]  = button_sniffer_handbrake_age_ms();
  doc["ok_button_pressed"] = button_sniffer_ok_pressed();
  doc["ok_button_age_ms"]  = button_sniffer_ok_age_ms();
  // v2.9.0 — Hazard + TC button sniffers
  doc["hazard_active"]     = button_sniffer_hazard_active();
  doc["hazard_age_ms"]     = button_sniffer_hazard_age_ms();
  doc["tc_button_pressed"] = button_sniffer_tc_pressed();
  doc["tc_button_age_ms"]  = button_sniffer_tc_age_ms();
  String out;
  serializeJson(doc, out);
  g_server.send(200, "application/json", out);
}

static void handle_settings_page() {
  // Serve the mobile settings HTML
  g_server.send_P(200, "text/html", SETTINGS_HTML);
}

static void handle_api_get_settings() {
  // Full settings dump (includes password — user is on private WiFi anyway)
  const Settings& s = settings_get();
  JsonDocument doc;
  doc["map_min_mbar"]              = s.map_min_mbar;
  doc["map_max_mbar"]              = s.map_max_mbar;
  doc["obd2_req_id"]               = s.obd2_req_id;
  doc["obd2_resp_id"]              = s.obd2_resp_id;
  doc["obd2_did_map"]              = s.obd2_did_map;
  doc["obd2_poll_hz"]              = s.obd2_poll_hz;
  doc["tx_rate_hz"]                = s.tx_rate_hz;
  doc["cluster_motor09_id"]        = s.cluster_motor09_id;
  doc["cluster_wba03_id"]          = s.cluster_wba03_id;
  doc["tx_enabled"]                = s.tx_enabled;
  doc["force_tx_always"]           = s.force_tx_always;
  doc["poll_ethanol"]              = s.poll_ethanol;
  doc["poll_haldex_blockage"]      = s.poll_haldex_blockage;
  doc["bench_test_enabled"]        = s.bench_test_enabled;
  doc["bench_test_bus"]            = s.bench_test_bus;
  doc["bench_rpm"]                 = s.bench_rpm;
  doc["bench_map_mbar"]            = s.bench_map_mbar;
  // v2.9.0: bench_display_value_pct + bench_force_override removed.
  // v2.9.0: 7 cluster_override / display_* settings removed.
  doc["cef_auto_enabled"]              = s.cef_auto_enabled;
  doc["cef_trigger_can_id"]            = s.cef_trigger_can_id;
  doc["cef_trigger_byte_idx"]          = s.cef_trigger_byte_idx;
  doc["cef_trigger_rest_value"]        = s.cef_trigger_rest_value;
  doc["cef_trigger_pressed_value"]     = s.cef_trigger_pressed_value;
  doc["cef_press_count"]               = s.cef_press_count;
  doc["cef_press_window_ms"]           = s.cef_press_window_ms;
  doc["wifi_enabled"]              = s.wifi_enabled;
  doc["wifi_ap_ssid"]              = s.wifi_ap_ssid;
  doc["wifi_ap_password"]          = s.wifi_ap_password;
  doc["haldex_enabled"]            = s.haldex_enabled;
  doc["haldex_bus"]                = s.haldex_bus;
  doc["haldex_state_id"]           = s.haldex_state_id;
  doc["haldex_cmd_id"]             = s.haldex_cmd_id;
  doc["haldex_transport"]          = s.haldex_transport;
  doc["haldex_espnow_peer_mac"]    = s.haldex_espnow_peer_mac;
  String out;
  serializeJson(doc, out);
  g_server.send(200, "application/json", out);
}

static void handle_api_set() {
  if (!g_server.hasArg("plain")) {
    g_server.send(400, "application/json", "{\"ok\":false,\"msg\":\"no body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, g_server.arg("plain"));
  if (err) {
    g_server.send(400, "application/json", "{\"ok\":false,\"msg\":\"bad JSON\"}");
    return;
  }
  const char* key = doc["key"];
  if (!key) {
    g_server.send(400, "application/json", "{\"ok\":false,\"msg\":\"no key\"}");
    return;
  }
  // Delegate to the shared dispatch in serial_proto.cpp
  bool ok = serial_proto_apply_setting(key, doc["value"]);
  String out = "{\"ok\":";
  out += ok ? "true" : "false";
  out += ok ? ",\"msg\":\"saved\"}" : ",\"msg\":\"unknown key or invalid value\"}";
  g_server.send(200, "application/json", out);
}

static void handle_cmd() {
  if (!g_server.hasArg("plain")) {
    g_server.send(400, "application/json", "{\"ok\":false,\"msg\":\"no body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, g_server.arg("plain"));
  if (err) {
    g_server.send(400, "application/json", "{\"ok\":false,\"msg\":\"bad JSON\"}");
    return;
  }
  const char* cmd = doc["cmd"];
  if (!cmd) {
    g_server.send(400, "application/json", "{\"ok\":false,\"msg\":\"no cmd\"}");
    return;
  }
  bool ok = false;
  const char* msg = "ok";
  if (strcmp(cmd, "clear_engine_fault") == 0) {
    ok = obd2_clear_engine_fault();
    msg = ok ? "Mode 04 broadcast sent" : "TX failed";
  } else if (strcmp(cmd, "set_haldex_mode") == 0) {
    uint8_t mode = doc["mode"] | 0;
    ok = haldex_link_set_mode(mode);
    msg = ok ? "mode set" : "Haldex disabled or TX failed";
  } else {
    msg = "unknown cmd";
  }
  String out = "{\"ok\":";
  out += ok ? "true" : "false";
  out += ",\"msg\":\"";
  out += msg;
  out += "\"}";
  g_server.send(200, "application/json", out);
}

static void handle_not_found() {
  g_server.send(404, "text/plain", "Not found");
}

// =============================================================
//  Lifecycle
// =============================================================
void wifi_ui_init() {
  // Module init only — actual AP start is in wifi_ui_apply().
  g_active = false;
}

void wifi_ui_apply() {
  const Settings& s = settings_get();

  if (!s.wifi_enabled) {
    if (g_active) {
      g_server.stop();
      WiFi.softAPdisconnect(true);
      Serial.println("[wifi_ui] AP stopped");
    }
    g_active = false;
    return;
  }

  // v2.6.1 FIX: always restart on apply. Previous check only tracked SSID
  // change, so password edits were silently ignored. Now we always tear
  // down + restart with current SSID/password.
  if (g_active) {
    g_server.stop();
    WiFi.softAPdisconnect(true);
    delay(100);
    g_active = false;
  }

  // ESP-NOW (Haldex transport=1) also uses WiFi. If both are needed, use APSTA mode.
  bool need_sta = (s.haldex_enabled && s.haldex_transport == 1);
  if (need_sta) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_AP);
  }

  bool started = WiFi.softAP(s.wifi_ap_ssid, s.wifi_ap_password);
  if (!started) {
    Serial.println("[wifi_ui] softAP FAILED");
    return;
  }
  g_curr_ssid = String(s.wifi_ap_ssid);

  g_server.on("/",                  HTTP_GET,  handle_root);
  g_server.on("/settings",          HTTP_GET,  handle_settings_page);     // v2.6.1: full mobile settings UI
  g_server.on("/api/status",        HTTP_GET,  handle_status);
  g_server.on("/api/get_settings",  HTTP_GET,  handle_api_get_settings);  // v2.6.1: full settings JSON dump
  g_server.on("/api/set",           HTTP_POST, handle_api_set);           // v2.6.1: update one setting
  g_server.on("/api/cmd",           HTTP_POST, handle_cmd);
  g_server.onNotFound(handle_not_found);
  g_server.begin();

  g_active = true;
  Serial.print("[wifi_ui] AP started: SSID=\"");
  Serial.print(s.wifi_ap_ssid);
  Serial.print("\" IP=http://");
  Serial.println(WiFi.softAPIP());
}

void wifi_ui_tick() {
  if (!g_active) return;
  g_server.handleClient();
}

// =============================================================
//  Diagnostic accessors
// =============================================================
bool   wifi_ui_is_active()    { return g_active; }
String wifi_ui_get_ip()       { return g_active ? WiFi.softAPIP().toString() : String(""); }
String wifi_ui_get_ssid()     { return g_active ? g_curr_ssid : String(""); }
uint8_t wifi_ui_get_n_clients() { return g_active ? WiFi.softAPgetStationNum() : 0; }
