/*
 * wifi_ui.cpp — implementation
 */
#include "wifi_ui.h"
#include "settings.h"
#include "obd2.h"
#include "lever_decoder.h"
#include "coolant.h"
#include "cluster_override.h"
#include "haldex_link.h"
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
</div>

<div class="actions">
  <button class="primary" onclick="cmd('clear_engine_fault')">🔧 Clear Engine Fault</button>
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
//  Route handlers
// =============================================================
static void handle_root() {
  g_server.send_P(200, "text/html", MOBILE_HTML);
}

static void handle_status() {
  // Build the same status payload as serial_proto::emit_status, just trimmed
  // to the fields the mobile UI actually displays.
  JsonDocument doc;
  doc["version"]     = "2.6.0";   // keep in sync with BUILD_VERSION
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
  String out;
  serializeJson(doc, out);
  g_server.send(200, "application/json", out);
}

static void handle_settings() {
  // Read-only settings dump
  const Settings& s = settings_get();
  JsonDocument doc;
  doc["map_min_mbar"]  = s.map_min_mbar;
  doc["map_max_mbar"]  = s.map_max_mbar;
  doc["tx_enabled"]    = s.tx_enabled;
  doc["wifi_enabled"]  = s.wifi_enabled;
  doc["wifi_ap_ssid"]  = s.wifi_ap_ssid;
  // password intentionally not returned
  String out;
  serializeJson(doc, out);
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

  if (g_active && g_curr_ssid == String(s.wifi_ap_ssid)) {
    // Already active with same SSID — nothing to do
    return;
  }

  // Stop previous AP if running with different SSID
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

  g_server.on("/",             HTTP_GET,  handle_root);
  g_server.on("/api/status",   HTTP_GET,  handle_status);
  g_server.on("/api/settings", HTTP_GET,  handle_settings);
  g_server.on("/api/cmd",      HTTP_POST, handle_cmd);
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
