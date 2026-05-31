/*
 * Serial protocol — implementation.
 *
 * Uses ArduinoJson library (install via Arduino IDE library manager).
 * If you prefer no external dependency, the parser is simple enough to write
 * by hand — but ArduinoJson is well-tested and fast.
 */
#include "serial_proto.h"
#include "settings.h"
#include "obd2.h"
#include "lever_decoder.h"
#include "coolant.h"
#include "haldex_link.h"
#include "haldex_espnow.h"
#include "wifi_ui.h"
#include "button_sniffer.h"
#include "haldex_modes.h"
#include "config.h"
#include <ArduinoJson.h>

#define BUILD_VERSION  "3.2.0"   // keep in sync with BOT32.ino line 2 + git tag
#define BUILD_DATE     __DATE__

static bool     subscribe_frames = false;     // off by default to avoid spam
static char     line_buf[512];
static uint16_t line_len = 0;
static uint32_t last_status_ms = 0;
static const uint32_t STATUS_INTERVAL_MS = 500;

// Mirrored from BOT32.ino via setters
static const char* current_mode_name = "BOOT";
static uint8_t     current_coolant_byte = 0x80;

// =============================================================
//  Helpers — emit JSON line
// =============================================================
static void emit_evt_boot() {
  JsonDocument doc;
  doc["evt"]     = "boot";
  doc["version"] = BUILD_VERSION;
  doc["build"]   = BUILD_DATE;
  serializeJson(doc, Serial);
  Serial.println();
}

static void emit_settings() {
  const Settings& s = settings_get();
  JsonDocument doc;
  doc["evt"]               = "settings";
  doc["map_min_mbar"]      = s.map_min_mbar;
  doc["map_max_mbar"]      = s.map_max_mbar;
  doc["obd2_req_id"]       = s.obd2_req_id;
  doc["obd2_resp_id"]      = s.obd2_resp_id;
  doc["obd2_did_map"]      = s.obd2_did_map;
  doc["obd2_poll_hz"]      = s.obd2_poll_hz;
  doc["poll_ethanol"]          = s.poll_ethanol;
  doc["poll_haldex_blockage"]  = s.poll_haldex_blockage;
  // v2.9.0: cluster_override settings removed.
  doc["cef_auto_enabled"]              = s.cef_auto_enabled;
  doc["cef_trigger_can_id"]            = s.cef_trigger_can_id;
  doc["cef_trigger_byte_idx"]          = s.cef_trigger_byte_idx;
  doc["cef_trigger_rest_value"]        = s.cef_trigger_rest_value;
  doc["cef_trigger_pressed_value"]     = s.cef_trigger_pressed_value;
  doc["cef_press_count"]               = s.cef_press_count;
  doc["cef_press_window_ms"]           = s.cef_press_window_ms;
  // v2.6.0: WiFi AP settings (live diag fields moved to emit_status in v2.6.1)
  doc["wifi_enabled"]      = s.wifi_enabled;
  doc["wifi_ap_ssid"]      = s.wifi_ap_ssid;
  doc["wifi_ap_password"]  = s.wifi_ap_password;
  doc["tx_rate_hz"]        = s.tx_rate_hz;
  doc["cluster_motor09_id"] = s.cluster_motor09_id;
  doc["cluster_wba03_id"]   = s.cluster_wba03_id;
  doc["tx_enabled"]        = s.tx_enabled;
  doc["listen_only_boot"]  = s.listen_only_boot;
  doc["force_tx_always"]    = s.force_tx_always;
  doc["block_airbag"]       = s.block_airbag;
  doc["bench_test_enabled"] = s.bench_test_enabled;
  doc["bench_rpm"]          = s.bench_rpm;
  doc["bench_map_mbar"]     = s.bench_map_mbar;
  doc["bench_test_bus"]     = s.bench_test_bus;
  // v2.9.0: bench_display_value_pct + bench_force_override removed.
  doc["tx_enabled_before_bench"] = s.tx_enabled_before_bench;  // v2.7.1 diag (read-only)
  doc["haldex_enabled"]     = s.haldex_enabled;
  // v3.2.0: haldex_bus/state_id/cmd_id/transport removed (ESP-NOW only).
  doc["haldex_espnow_peer_mac"] = s.haldex_espnow_peer_mac;
  doc["bot32_mac"]          = haldex_espnow_get_my_mac();   // for user pairing
  serializeJson(doc, Serial);
  Serial.println();
}

static void emit_status() {
  JsonDocument doc;
  doc["evt"] = "status";
  doc["uptime_ms"]    = millis();
  doc["mode"]         = current_mode_name;

  // ALWAYS compute the live coolant byte that BOT32 WOULD send right now,
  // regardless of whether we're actually TXing. This makes the UI useful
  // for monitoring/calibration even in SILENT mode (P/R/N/D).
  const Settings& s_status = settings_get();
  float live_map;
  if (s_status.bench_test_enabled) {
    live_map = (float)s_status.bench_map_mbar;     // bench slider drives it
  } else {
    live_map = obd2_get_last_map_mbar();           // real OBD2 MAP
    if (live_map < 0.0f) live_map = s_status.map_min_mbar;  // stale fallback
  }
  uint8_t live_coolant_byte = coolant_map_mbar_to_byte(
    live_map, s_status.map_min_mbar, s_status.map_max_mbar
  );
  doc["coolant_byte"] = live_coolant_byte;

  // Whether Motor_09 is ACTUALLY being transmitted right now (UI displays
  // this as a status indicator alongside the always-visible coolant value).
  bool motor09_tx_active = false;
  if (s_status.tx_enabled) {
    if (s_status.bench_test_enabled) {
      motor09_tx_active = true;
    } else if (strcmp(current_mode_name, "BOOST") == 0) {
      motor09_tx_active = true;
    } else if (s_status.force_tx_always
               && strcmp(current_mode_name, "BOOT") != 0
               && strcmp(current_mode_name, "SAFE_FAULT") != 0) {
      motor09_tx_active = true;
    }
  }
  doc["motor09_tx_active"] = motor09_tx_active;
  // Real coolant temp sniffed from Motor_09 on cluster bus (CAN0)
  doc["real_coolant_c"]     = coolant_get_real_temp_c();   // -1 if no data
  doc["real_coolant_age_ms"] = coolant_get_real_age_ms();
  doc["lever"]        = String(lever_get());
  doc["gear"]         = lever_get_gear();
  doc["lever_age_ms"] = lever_get_age_ms();
  float map = obd2_get_last_map_mbar();
  doc["map_mbar"]     = map >= 0 ? map : (float)-1;
  doc["map_age_ms"]   = obd2_get_map_age_ms();

  // v2.1: extra UDS values
  float ethanol = obd2_get_last_ethanol_pct();
  doc["ethanol_pct"]      = ethanol >= 0 ? ethanol : (float)-1;
  doc["ethanol_age_ms"]   = obd2_get_ethanol_age_ms();

  float hdx_blk = obd2_get_last_haldex_blockage_pct();
  doc["haldex_blockage_pct"]    = hdx_blk >= 0 ? hdx_blk : (float)-1;
  doc["haldex_blockage_raw"]    = obd2_get_last_haldex_blockage_raw();
  doc["haldex_blockage_age_ms"] = obd2_get_haldex_blockage_age_ms();

  // v2.8.0 — three new temperatures (UDS polled, round-robin)
  // Sentinel -1000 means "no data yet". UI must check for this.
  float dsg_oil = obd2_get_last_dsg_oil_c();
  doc["dsg_oil_c"]      = dsg_oil > -999.0f ? dsg_oil : (float)-1000;
  doc["dsg_oil_age_ms"] = obd2_get_dsg_oil_age_ms();

  float egt = obd2_get_last_egt_c();
  doc["egt_c"]      = egt > -999.0f ? egt : (float)-1000;
  doc["egt_age_ms"] = obd2_get_egt_age_ms();

  // v2.10.0: engine oil temp removed from status.

  // v2.8.0 — two new sniffer fields (cluster CAN passive listeners)
  doc["handbrake_active"]   = button_sniffer_handbrake_active();
  doc["handbrake_age_ms"]   = button_sniffer_handbrake_age_ms();
  doc["ok_button_pressed"]  = button_sniffer_ok_pressed();
  doc["ok_button_age_ms"]   = button_sniffer_ok_age_ms();
  // v2.9.0 — Hazard switch + Traction Control button sniffers
  doc["hazard_active"]      = button_sniffer_hazard_active();
  doc["hazard_age_ms"]      = button_sniffer_hazard_age_ms();
  doc["tc_button_pressed"]  = button_sniffer_tc_pressed();
  doc["tc_button_age_ms"]   = button_sniffer_tc_age_ms();

  // v2.1: clear-all-DTCs progress (only meaningful while running)
  doc["clear_all_dtcs_in_progress"] = obd2_clear_all_dtcs_in_progress();
  doc["clear_all_dtcs_progress"]    = obd2_clear_all_dtcs_progress_pct();
  doc["clear_all_dtcs_ecu"]         = obd2_clear_all_dtcs_current_ecu();

  // v2.9.0: cluster_override diagnostics removed (feature deleted).

  // v2.6.1: WiFi AP live diagnostics (moved from emit_settings so the UI
  // refreshes the status pill in real-time, not only on settings change).
  doc["wifi_active"]       = wifi_ui_is_active();
  doc["wifi_ip"]           = wifi_ui_get_ip();
  doc["wifi_clients"]      = wifi_ui_get_n_clients();

  // Haldex link state (from external MITM module, see haldex_link.cpp)
  HaldexState hx = haldex_link_get_state();
  uint32_t hx_age = haldex_link_get_age_ms();
  JsonObject hx_obj = doc["haldex"].to<JsonObject>();
  hx_obj["valid"]               = hx.valid;
  hx_obj["age_ms"]              = hx_age;
  // v3.1.0: explicit online flag (STATE arrives at 5 Hz; stale => link down)
  hx_obj["online"]              = hx.valid && (hx_age < HALDEX_LINK_STALE_MS);
  // v3.1.0: the mode decided locally by haldex_modes (immediate UI feedback,
  // independent of the MITM's echoed mode) + the desired telltale blink phase.
  hx_obj["local_mode"]          = haldex_modes_get();
  hx_obj["local_mode_name"]     = haldex_mode_name(haldex_modes_get());
  hx_obj["telltale_on"]         = haldex_modes_telltale_on();
  hx_obj["current_mode"]        = hx.current_mode;
  hx_obj["current_mode_name"]   = haldex_mode_name(hx.current_mode);
  hx_obj["pump_engagement_pct"] = hx.pump_engagement_pct;
  hx_obj["lock_target_pct"]     = hx.lock_target_pct;
  hx_obj["vehicle_kmh"]         = hx.vehicle_kmh;
  hx_obj["pedal_pct"]           = hx.pedal_pct;
  // v3.2.0: passthrough — actual (from X2 STATE) + desired (what we commanded)
  hx_obj["passthrough"]         = hx.passthrough;
  hx_obj["passthrough_desired"] = haldex_modes_get_passthrough_desired();
  JsonArray raw = hx_obj["raw"].to<JsonArray>();
  for (uint8_t i = 0; i < hx.len; i++) raw.add(hx.raw[i]);

  CanStats sc = can_get_stats(CAN_CLUSTER);
  CanStats so = can_get_stats(CAN_OBD2);
  JsonObject cluster_stats = doc["cluster"].to<JsonObject>();
  cluster_stats["tx_ok"]   = sc.tx_ok;
  cluster_stats["tx_fail"] = sc.tx_fail;
  cluster_stats["rx"]      = sc.rx_count;
  cluster_stats["errors"]  = sc.bus_errors;
  JsonObject obd2_stats = doc["obd2"].to<JsonObject>();
  obd2_stats["tx_ok"]   = so.tx_ok;
  obd2_stats["tx_fail"] = so.tx_fail;
  obd2_stats["rx"]      = so.rx_count;
  obd2_stats["errors"]  = so.bus_errors;
  // OBD2-specific UDS diagnostic counters
  obd2_stats["uds_sent"]       = obd2_get_queries_sent();
  obd2_stats["uds_resp_ok"]    = obd2_get_responses_ok();
  obd2_stats["uds_resp_bad"]   = obd2_get_responses_garbled();

  serializeJson(doc, Serial);
  Serial.println();
}

static void emit_ack(const char* for_cmd, bool ok, const char* msg = nullptr) {
  JsonDocument doc;
  doc["evt"] = "ack";
  doc["for"] = for_cmd;
  doc["ok"]  = ok;
  if (msg) doc["msg"] = msg;
  serializeJson(doc, Serial);
  Serial.println();
}

static void emit_frame(CanChannel ch, const char* dir, const CanFrame& f) {
  JsonDocument doc;
  doc["evt"] = "frame";
  doc["bus"] = (ch == CAN_CLUSTER) ? "cluster" : "obd2";
  doc["dir"] = dir;
  doc["id"]  = f.id;
  doc["len"] = f.len;
  JsonArray data = doc["data"].to<JsonArray>();
  for (uint8_t i = 0; i < f.len; i++) data.add(f.data[i]);
  doc["ts_ms"] = f.timestamp;
  serializeJson(doc, Serial);
  Serial.println();
}

// =============================================================
//  CAN RX callback — mirrors frames to PC if subscribed
// =============================================================
static void on_rx_for_pc(CanChannel ch, const CanFrame& f) {
  if (subscribe_frames) {
    emit_frame(ch, "rx", f);
  }
}

// =============================================================
//  Command dispatcher
// =============================================================
static void handle_cmd(const char* line) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    emit_ack("?", false, "bad JSON");
    return;
  }
  const char* cmd = doc["cmd"];
  if (!cmd) {
    emit_ack("?", false, "missing cmd");
    return;
  }

  if (strcmp(cmd, "ping") == 0) {
    JsonDocument r;
    r["evt"] = "pong";
    serializeJson(r, Serial);
    Serial.println();
    return;
  }

  if (strcmp(cmd, "get_status") == 0) {
    emit_status();
    return;
  }

  if (strcmp(cmd, "get_settings") == 0) {
    emit_settings();
    return;
  }

  if (strcmp(cmd, "factory_reset") == 0) {
    settings_reset_to_defaults();
    emit_ack("factory_reset", true);
    emit_settings();
    return;
  }

  if (strcmp(cmd, "subscribe_frames") == 0) {
    subscribe_frames = doc["enabled"] | false;
    emit_ack("subscribe_frames", true);
    return;
  }

  // Direct action to set the Haldex mode. v3.1.0: routed through haldex_modes
  // so the manual command + the physical combo logic stay consistent.
  if (strcmp(cmd, "set_haldex_mode") == 0) {
    uint8_t mode = doc["mode"] | 0;
    bool ok = haldex_modes_set_manual(mode);
    emit_ack("set_haldex_mode", ok, ok ? haldex_mode_name(mode) : "refused (disabled / bad mode)");
    return;
  }

  // v3.2.0: arm/disarm the MITM passthrough on the X2 (ESP-NOW)
  if (strcmp(cmd, "set_haldex_passthrough") == 0) {
    bool pt = doc["on"] | true;   // default safe (true = passthrough ON)
    bool ok = haldex_modes_set_passthrough(pt);
    emit_ack("set_haldex_passthrough", ok, ok ? (pt ? "passthrough ON" : "MITM armed") : "refused (disabled)");
    return;
  }

  // v2.1: Clear engine fault — OBD-II Mode 04 broadcast on 0x700
  if (strcmp(cmd, "clear_engine_fault") == 0) {
    bool ok = obd2_clear_engine_fault();
    emit_ack("clear_engine_fault", ok, ok ? "Mode 04 broadcast sent" : "TX failed");
    return;
  }

  // v2.1: Clear DTC on ALL modules — non-blocking state machine
  if (strcmp(cmd, "clear_all_dtcs") == 0) {
    bool ok = obd2_clear_all_dtcs();
    emit_ack("clear_all_dtcs", ok, ok ? "started" : "already running or busy");
    return;
  }

  if (strcmp(cmd, "set") == 0) {
    const char* key = doc["key"];
    if (!key) { emit_ack("set", false, "no key"); return; }
    bool ok = serial_proto_apply_setting(key, doc["value"]);
    emit_ack("set", ok);
    if (ok) emit_settings();
    return;
  }

  emit_ack(cmd, false, "unknown cmd");
}

// =============================================================
//  v2.6.1: shared setting dispatch — used by serial JSON "set" cmd AND
//  by the WiFi HTTP /api/set endpoint. One source of truth for which
//  keys are valid + their default coercion values.
// =============================================================
bool serial_proto_apply_setting(const char* key, JsonVariantConst v) {
  if (!key) return false;
  bool ok = false;
  if      (strcmp(key, "map_min_mbar")          == 0) ok = settings_set_map_min_mbar(v    | 0.0f);
  else if (strcmp(key, "map_max_mbar")          == 0) ok = settings_set_map_max_mbar(v    | 0.0f);
  else if (strcmp(key, "obd2_did_map")          == 0) ok = settings_set_obd2_did_map(v    | 0);
  else if (strcmp(key, "obd2_req_id")           == 0) ok = settings_set_obd2_req_id(v     | 0);
  else if (strcmp(key, "obd2_resp_id")          == 0) ok = settings_set_obd2_resp_id(v    | 0);
  else if (strcmp(key, "obd2_poll_hz")          == 0) ok = settings_set_obd2_poll_hz(v    | 5);
  else if (strcmp(key, "poll_ethanol")          == 0) ok = settings_set_poll_ethanol(v          | false);
  else if (strcmp(key, "poll_haldex_blockage")  == 0) ok = settings_set_poll_haldex_blockage(v  | false);
  // v2.9.0: cluster_override + display_* setters removed (feature deleted).
  else if (strcmp(key, "cef_auto_enabled")          == 0) ok = settings_set_cef_auto_enabled(v          | true);
  else if (strcmp(key, "cef_trigger_can_id")        == 0) ok = settings_set_cef_trigger_can_id(v        | 0x366);
  else if (strcmp(key, "cef_trigger_byte_idx")      == 0) ok = settings_set_cef_trigger_byte_idx(v      | 2);
  else if (strcmp(key, "cef_trigger_rest_value")    == 0) ok = settings_set_cef_trigger_rest_value(v    | 0);
  else if (strcmp(key, "cef_trigger_pressed_value") == 0) ok = settings_set_cef_trigger_pressed_value(v | 0x10);
  else if (strcmp(key, "cef_press_count")           == 0) ok = settings_set_cef_press_count(v           | 3);
  else if (strcmp(key, "cef_press_window_ms")       == 0) ok = settings_set_cef_press_window_ms(v       | 4000);
  else if (strcmp(key, "wifi_enabled")              == 0) { ok = settings_set_wifi_enabled(v              | false); if (ok) wifi_ui_apply(); }
  else if (strcmp(key, "wifi_ap_ssid")              == 0) { ok = settings_set_wifi_ap_ssid(v              | ""); if (ok) wifi_ui_apply(); }
  else if (strcmp(key, "wifi_ap_password")          == 0) { ok = settings_set_wifi_ap_password(v          | ""); if (ok) wifi_ui_apply(); }
  else if (strcmp(key, "tx_rate_hz")         == 0) ok = settings_set_tx_rate_hz(v         | 20);
  else if (strcmp(key, "tx_enabled")         == 0) ok = settings_set_tx_enabled(v         | false);
  else if (strcmp(key, "force_tx_always")    == 0) ok = settings_set_force_tx_always(v    | false);
  else if (strcmp(key, "block_airbag")       == 0) ok = settings_set_block_airbag(v       | true);
  else if (strcmp(key, "cluster_motor09_id") == 0) ok = settings_set_cluster_motor09_id(v | 0);
  else if (strcmp(key, "cluster_wba03_id")   == 0) ok = settings_set_cluster_wba03_id(v   | 0);
  else if (strcmp(key, "bench_test_enabled") == 0) ok = settings_set_bench_test_enabled(v | false);
  else if (strcmp(key, "bench_rpm")          == 0) ok = settings_set_bench_rpm(v          | 0);
  else if (strcmp(key, "bench_map_mbar")     == 0) ok = settings_set_bench_map_mbar(v     | 0);
  else if (strcmp(key, "bench_test_bus")     == 0) ok = settings_set_bench_test_bus(v     | 0);
  // v2.9.0: bench_display_value_pct + bench_force_override setters removed.
  else if (strcmp(key, "haldex_enabled")        == 0) ok = settings_set_haldex_enabled(v     | false);
  // v3.2.0: haldex_bus/state_id/cmd_id/transport setters removed (ESP-NOW only).
  else if (strcmp(key, "haldex_espnow_peer_mac") == 0) ok = settings_set_haldex_espnow_peer_mac(v | "");
  return ok;
}

// =============================================================
//  Public API
// =============================================================
void serial_proto_init() {
  emit_evt_boot();
  emit_settings();
  can_register_listener(CAN_CLUSTER, on_rx_for_pc);
  can_register_listener(CAN_OBD2,    on_rx_for_pc);
}

void serial_proto_poll() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (line_len > 0) {
        line_buf[line_len] = 0;
        handle_cmd(line_buf);
        line_len = 0;
      }
    } else if (line_len < sizeof(line_buf) - 1) {
      line_buf[line_len++] = c;
    } else {
      // overflow - discard
      line_len = 0;
    }
  }
}

void serial_proto_tick() {
  uint32_t now = millis();
  if (now - last_status_ms >= STATUS_INTERVAL_MS) {
    emit_status();
    last_status_ms = now;
  }
}

void serial_proto_report_tx(CanChannel ch, const CanFrame& f) {
  if (subscribe_frames) {
    emit_frame(ch, "tx", f);
  }
}

void serial_proto_set_mode(const char* mode_name) {
  current_mode_name = mode_name;
}

void serial_proto_set_coolant_byte(uint8_t b) {
  current_coolant_byte = b;
}

void serial_proto_log(const char* level, const char* msg) {
  JsonDocument doc;
  doc["evt"]   = "log";
  doc["level"] = level;
  doc["msg"]   = msg;
  serializeJson(doc, Serial);
  Serial.println();
}
