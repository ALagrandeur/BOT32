/*
 * Settings persistence — implementation using ESP32 Preferences (NVS).
 */
#include "settings.h"
#include "config.h"
#include <Preferences.h>

static Preferences prefs;
static Settings current;

#define NVS_NAMESPACE  "bot32"
#define SETTINGS_VERSION 18  // v2.9.0: removed 9 cluster_override-related fields

static Settings make_defaults() {
  Settings s;
  s.map_min_mbar      = MAP_MIN_MBAR_DEFAULT;
  s.map_max_mbar      = MAP_MAX_MBAR_DEFAULT;
  s.obd2_req_id       = CAN_ID_OBD2_REQ;
  s.obd2_resp_id      = CAN_ID_OBD2_RESP;
  s.obd2_did_map      = UDS_DID_MAP;
  // v2.8.0: bumped from 15 -> 30. With 6 round-robin slots (MAP + ethanol +
  // Haldex + DSG oil + EGT + Engine oil), 30 Hz total gives each slot ~5 Hz —
  // matches the v2.4.0 per-slot rate when there were only 3 slots.
  s.obd2_poll_hz      = 30;
  s.poll_ethanol            = true;   // v2.4.0: ON by default (no UI toggle)
  s.poll_haldex_blockage    = true;   // v2.4.0: ON by default (no UI toggle)
  // v2.9.0: cluster_override defaults removed (feature deleted).
  // v2.5.1: clear-engine-fault auto-trigger config — Hazard button, 3x in 4s
  s.cef_auto_enabled          = true;       // master toggle, ON by default
  s.cef_trigger_can_id        = 0x366;      // Hazard (Blinkmodi_01)
  s.cef_trigger_byte_idx      = 2;          // D3
  s.cef_trigger_rest_value    = 0x00;       // Hazard OFF
  s.cef_trigger_pressed_value = 0x10;       // Hazard ON bit
  s.cef_press_count           = 3;          // 3 ON/OFF cycles
  s.cef_press_window_ms       = 4000;       // within 4 seconds
  // v2.6.0: WiFi AP defaults
  s.wifi_enabled              = false;      // OFF by default — must be enabled by user
  strncpy(s.wifi_ap_ssid,     "BOT32", sizeof(s.wifi_ap_ssid) - 1);
  s.wifi_ap_ssid[sizeof(s.wifi_ap_ssid) - 1] = 0;
  strncpy(s.wifi_ap_password, "BOT32-2026", sizeof(s.wifi_ap_password) - 1);
  s.wifi_ap_password[sizeof(s.wifi_ap_password) - 1] = 0;
  s.tx_rate_hz        = 1000 / MOTOR_09_TX_INTERVAL_MS;
  s.cluster_motor09_id = CAN_ID_MOTOR_09;
  s.cluster_wba03_id   = CAN_ID_WBA_03;
  s.tx_enabled        = true;
  s.listen_only_boot  = true;
  s.force_tx_always   = false;   // default: only TX in BOOST mode (S/M lever)
  s.block_airbag      = true;    // default: airbag IDs blocked (SAFETY)
  s.bench_test_enabled = false;  // default: bench mode OFF
  s.bench_rpm          = 1500;
  s.bench_map_mbar     = 1500;
  s.bench_test_bus     = 0;      // default: CAN_CLUSTER
  // v2.9.0: bench_display_value_pct + bench_force_override removed.
  s.tx_enabled_before_bench = true;  // v2.7.1: default safe (TX restored ON if no bench transition)
  s.haldex_enabled     = false;  // default: Haldex link OFF (safety)
  // v3.2.0: CAN-transport defaults removed (ESP-NOW only).
  s.haldex_espnow_peer_mac[0] = 0;  // default empty => use broadcast
  s.version           = SETTINGS_VERSION;
  return s;
}

void settings_init() {
  prefs.begin(NVS_NAMESPACE, false);
  uint8_t v = prefs.getUChar("version", 0);
  if (v != SETTINGS_VERSION) {
    Serial.print("[NVS] No saved settings (or wrong version=");
    Serial.print(v);
    Serial.println("), using defaults");
    current = make_defaults();
    // Save defaults
    settings_reset_to_defaults();
    return;
  }
  current.map_min_mbar      = prefs.getFloat("map_min", MAP_MIN_MBAR_DEFAULT);
  current.map_max_mbar      = prefs.getFloat("map_max", MAP_MAX_MBAR_DEFAULT);
  current.obd2_req_id       = prefs.getUShort("obd_req", CAN_ID_OBD2_REQ);
  current.obd2_resp_id      = prefs.getUShort("obd_resp", CAN_ID_OBD2_RESP);
  current.obd2_did_map      = prefs.getUShort("obd_did", UDS_DID_MAP);
  current.obd2_poll_hz      = prefs.getUShort("obd_hz", 30);          // v2.8.0 default (6 slots)
  current.poll_ethanol          = prefs.getBool("p_etoh", true);       // v2.4.0 default ON
  current.poll_haldex_blockage  = prefs.getBool("p_hdxb", true);       // v2.4.0 default ON
  // v2.9.0: cluster_override NVS load removed (NVS keys "co_*" abandoned —
  // SETTINGS_VERSION bump 17->18 will trigger a one-time reset that clears them).
  // v2.5.1: clear-engine-fault auto-trigger config (roadmap detection)
  current.cef_auto_enabled          = prefs.getBool("cef_en", true);
  current.cef_trigger_can_id        = prefs.getUShort("cef_id", 0x366);
  current.cef_trigger_byte_idx      = prefs.getUChar("cef_bi", 2);
  current.cef_trigger_rest_value    = prefs.getUChar("cef_rv", 0x00);
  current.cef_trigger_pressed_value = prefs.getUChar("cef_pv", 0x10);
  current.cef_press_count           = prefs.getUChar("cef_pc", 3);
  current.cef_press_window_ms       = prefs.getUShort("cef_pw", 4000);
  // v2.6.0: WiFi AP settings
  current.wifi_enabled              = prefs.getBool("wf_en", false);
  {
    String ssid = prefs.getString("wf_ssid", "BOT32");
    strncpy(current.wifi_ap_ssid, ssid.c_str(), sizeof(current.wifi_ap_ssid) - 1);
    current.wifi_ap_ssid[sizeof(current.wifi_ap_ssid) - 1] = 0;
    String pw = prefs.getString("wf_pw", "BOT32-2026");
    strncpy(current.wifi_ap_password, pw.c_str(), sizeof(current.wifi_ap_password) - 1);
    current.wifi_ap_password[sizeof(current.wifi_ap_password) - 1] = 0;
  }
  current.tx_rate_hz        = prefs.getUShort("tx_hz", 1000 / MOTOR_09_TX_INTERVAL_MS);
  current.cluster_motor09_id = prefs.getUShort("cl_m09", CAN_ID_MOTOR_09);
  current.cluster_wba03_id   = prefs.getUShort("cl_wba", CAN_ID_WBA_03);
  current.tx_enabled        = prefs.getBool("tx_en", true);
  current.listen_only_boot  = prefs.getBool("lo_boot", true);
  current.force_tx_always   = prefs.getBool("fx_tx", false);
  current.block_airbag      = prefs.getBool("blk_ab", true);
  current.bench_test_enabled = prefs.getBool("bch_en", false);
  current.bench_rpm          = prefs.getUShort("bch_rpm", 1500);
  current.bench_map_mbar     = prefs.getUShort("bch_map", 1500);
  current.bench_test_bus     = prefs.getUChar("bch_bus", 0);
  // v2.9.0: bench_display_value_pct + bench_force_override NVS load removed.
  current.tx_enabled_before_bench = prefs.getBool("tx_pre_bch", true);  // v2.7.1
  current.haldex_enabled     = prefs.getBool("hdx_en", false);
  // v3.2.0: CAN-transport NVS load removed (ESP-NOW only). Orphan keys
  // hdx_bus/hdx_sid/hdx_cid/hdx_tr remain in NVS but are no longer read.
  {
    String mac = prefs.getString("hdx_mac", "");
    strncpy(current.haldex_espnow_peer_mac, mac.c_str(),
            sizeof(current.haldex_espnow_peer_mac) - 1);
    current.haldex_espnow_peer_mac[sizeof(current.haldex_espnow_peer_mac) - 1] = 0;
  }
  current.version           = SETTINGS_VERSION;

  // v2.3.3: SAFETY HARDCODE — block_airbag is forced ON at every boot
  // regardless of NVS value or UI input. The UI no longer exposes this
  // toggle. If you ever need to TX on 0x040/0x572 for diagnostic work,
  // remove this override AND restore the UI checkbox.
  if (!current.block_airbag) {
    Serial.println("[NVS] block_airbag was OFF in NVS -> forcing ON (v2.3.3 safety)");
    current.block_airbag = true;
    prefs.putBool("blk_ab", true);
  }

  Serial.println("[NVS] Settings loaded from flash");
}

const Settings& settings_get() {
  return current;
}

static bool save_float(const char* key, float v) {
  return prefs.putFloat(key, v) > 0;
}
static bool save_ushort(const char* key, uint16_t v) {
  return prefs.putUShort(key, v) > 0;
}
static bool save_bool(const char* key, bool v) {
  return prefs.putBool(key, v);
}

bool settings_set_map_min_mbar(float v) {
  current.map_min_mbar = v;
  return save_float("map_min", v);
}
bool settings_set_map_max_mbar(float v) {
  current.map_max_mbar = v;
  return save_float("map_max", v);
}
bool settings_set_obd2_did_map(uint16_t v) {
  current.obd2_did_map = v;
  return save_ushort("obd_did", v);
}
bool settings_set_obd2_poll_hz(uint16_t v) {
  current.obd2_poll_hz = v;
  return save_ushort("obd_hz", v);
}
bool settings_set_poll_ethanol(bool v) {
  current.poll_ethanol = v;
  return save_bool("p_etoh", v);
}
bool settings_set_poll_haldex_blockage(bool v) {
  current.poll_haldex_blockage = v;
  return save_bool("p_hdxb", v);
}
// v2.9.0: 7 cluster_override setters removed (feature deleted).
bool settings_set_cef_auto_enabled(bool v) {
  current.cef_auto_enabled = v;
  return save_bool("cef_en", v);
}
bool settings_set_cef_trigger_can_id(uint16_t v) {
  current.cef_trigger_can_id = v;
  return prefs.putUShort("cef_id", v) > 0;
}
bool settings_set_cef_trigger_byte_idx(uint8_t v) {
  if (v > 7) v = 7;
  current.cef_trigger_byte_idx = v;
  return prefs.putUChar("cef_bi", v) > 0;
}
bool settings_set_cef_trigger_rest_value(uint8_t v) {
  current.cef_trigger_rest_value = v;
  return prefs.putUChar("cef_rv", v) > 0;
}
bool settings_set_cef_trigger_pressed_value(uint8_t v) {
  current.cef_trigger_pressed_value = v;
  return prefs.putUChar("cef_pv", v) > 0;
}
bool settings_set_cef_press_count(uint8_t v) {
  if (v < 1) v = 1;
  if (v > 10) v = 10;
  current.cef_press_count = v;
  return prefs.putUChar("cef_pc", v) > 0;
}
bool settings_set_cef_press_window_ms(uint16_t v) {
  if (v < 500) v = 500;
  if (v > 30000) v = 30000;
  current.cef_press_window_ms = v;
  return prefs.putUShort("cef_pw", v) > 0;
}
bool settings_set_wifi_enabled(bool v) {
  current.wifi_enabled = v;
  return save_bool("wf_en", v);
}
bool settings_set_wifi_ap_ssid(const char* v) {
  if (!v || strlen(v) == 0) return false;
  strncpy(current.wifi_ap_ssid, v, sizeof(current.wifi_ap_ssid) - 1);
  current.wifi_ap_ssid[sizeof(current.wifi_ap_ssid) - 1] = 0;
  return prefs.putString("wf_ssid", current.wifi_ap_ssid) > 0;
}
bool settings_set_wifi_ap_password(const char* v) {
  if (!v || strlen(v) < 8) return false;  // WPA2 minimum 8 chars
  strncpy(current.wifi_ap_password, v, sizeof(current.wifi_ap_password) - 1);
  current.wifi_ap_password[sizeof(current.wifi_ap_password) - 1] = 0;
  return prefs.putString("wf_pw", current.wifi_ap_password) > 0;
}
bool settings_set_tx_rate_hz(uint16_t v) {
  current.tx_rate_hz = v;
  return save_ushort("tx_hz", v);
}
bool settings_set_tx_enabled(bool v) {
  current.tx_enabled = v;
  return save_bool("tx_en", v);
}
bool settings_set_force_tx_always(bool v) {
  current.force_tx_always = v;
  return save_bool("fx_tx", v);
}
bool settings_set_block_airbag(bool v) {
  current.block_airbag = v;
  return save_bool("blk_ab", v);
}
bool settings_set_cluster_motor09_id(uint16_t v) {
  current.cluster_motor09_id = v;
  return save_ushort("cl_m09", v);
}
bool settings_set_cluster_wba03_id(uint16_t v) {
  current.cluster_wba03_id = v;
  return save_ushort("cl_wba", v);
}
bool settings_set_obd2_req_id(uint16_t v) {
  current.obd2_req_id = v;
  return save_ushort("obd_req", v);
}
bool settings_set_obd2_resp_id(uint16_t v) {
  current.obd2_resp_id = v;
  return save_ushort("obd_resp", v);
}
bool settings_set_bench_test_enabled(bool v) {
  bool was_enabled = current.bench_test_enabled;
  if (v == was_enabled) {
    // No transition — just persist (defensive)
    current.bench_test_enabled = v;
    return save_bool("bch_en", v);
  }
  // v2.7.1: bench transitions auto-manage tx_enabled
  if (v && !was_enabled) {
    // ENTERING bench: memorize current tx_enabled, then force ON
    current.tx_enabled_before_bench = current.tx_enabled;
    prefs.putBool("tx_pre_bch", current.tx_enabled_before_bench);
    Serial.print("[bench] Entering bench mode. Saved tx_enabled=");
    Serial.print(current.tx_enabled_before_bench ? "true" : "false");
    Serial.println(", forcing tx_enabled=true.");
    current.tx_enabled = true;
    save_bool("tx_en", true);
  } else if (!v && was_enabled) {
    // EXITING bench: restore tx_enabled to saved value
    Serial.print("[bench] Exiting bench mode. Restoring tx_enabled=");
    Serial.println(current.tx_enabled_before_bench ? "true" : "false");
    current.tx_enabled = current.tx_enabled_before_bench;
    save_bool("tx_en", current.tx_enabled);
  }
  current.bench_test_enabled = v;
  return save_bool("bch_en", v);
}
bool settings_set_bench_rpm(uint16_t v) {
  current.bench_rpm = (v > 8000) ? 8000 : v;
  return save_ushort("bch_rpm", current.bench_rpm);
}
bool settings_set_bench_map_mbar(uint16_t v) {
  current.bench_map_mbar = (v > 3000) ? 3000 : v;
  return save_ushort("bch_map", current.bench_map_mbar);
}
bool settings_set_bench_test_bus(uint8_t v) {
  current.bench_test_bus = (v > 1) ? 0 : v;
  return prefs.putUChar("bch_bus", current.bench_test_bus) > 0;
}
// v2.9.0: bench_display_value_pct + bench_force_override setters removed.
bool settings_set_haldex_enabled(bool v) {
  current.haldex_enabled = v;
  return save_bool("hdx_en", v);
}
// v3.2.0: haldex_bus / state_id / cmd_id / transport setters removed (ESP-NOW only).
bool settings_set_haldex_espnow_peer_mac(const char* v) {
  if (!v) return false;
  strncpy(current.haldex_espnow_peer_mac, v,
          sizeof(current.haldex_espnow_peer_mac) - 1);
  current.haldex_espnow_peer_mac[sizeof(current.haldex_espnow_peer_mac) - 1] = 0;
  return prefs.putString("hdx_mac", current.haldex_espnow_peer_mac) > 0;
}

void settings_reset_to_defaults() {
  current = make_defaults();
  prefs.putFloat("map_min", current.map_min_mbar);
  prefs.putFloat("map_max", current.map_max_mbar);
  prefs.putUShort("obd_req", current.obd2_req_id);
  prefs.putUShort("obd_resp", current.obd2_resp_id);
  prefs.putUShort("obd_did", current.obd2_did_map);
  prefs.putUShort("obd_hz", current.obd2_poll_hz);
  prefs.putBool("p_etoh", current.poll_ethanol);
  prefs.putBool("p_hdxb", current.poll_haldex_blockage);
  // v2.9.0: cluster_override NVS writes removed.
  prefs.putBool("cef_en", current.cef_auto_enabled);
  prefs.putUShort("cef_id", current.cef_trigger_can_id);
  prefs.putUChar("cef_bi", current.cef_trigger_byte_idx);
  prefs.putUChar("cef_rv", current.cef_trigger_rest_value);
  prefs.putUChar("cef_pv", current.cef_trigger_pressed_value);
  prefs.putUChar("cef_pc", current.cef_press_count);
  prefs.putUShort("cef_pw", current.cef_press_window_ms);
  prefs.putBool("wf_en", current.wifi_enabled);
  prefs.putString("wf_ssid", current.wifi_ap_ssid);
  prefs.putString("wf_pw", current.wifi_ap_password);
  prefs.putUShort("tx_hz", current.tx_rate_hz);
  prefs.putUShort("cl_m09", current.cluster_motor09_id);
  prefs.putUShort("cl_wba", current.cluster_wba03_id);
  prefs.putBool("tx_en", current.tx_enabled);
  prefs.putBool("lo_boot", current.listen_only_boot);
  prefs.putBool("fx_tx", current.force_tx_always);
  prefs.putBool("blk_ab", current.block_airbag);
  prefs.putBool("bch_en", current.bench_test_enabled);
  prefs.putUShort("bch_rpm", current.bench_rpm);
  prefs.putUShort("bch_map", current.bench_map_mbar);
  prefs.putUChar("bch_bus", current.bench_test_bus);
  // v2.9.0: bench_display_value_pct + bench_force_override NVS writes removed.
  prefs.putBool("tx_pre_bch", current.tx_enabled_before_bench);
  prefs.putBool("hdx_en", current.haldex_enabled);
  // v3.2.0: CAN-transport NVS writes removed (ESP-NOW only).
  prefs.putString("hdx_mac", current.haldex_espnow_peer_mac);
  prefs.putUChar("version", current.version);
  Serial.println("[NVS] Settings reset to defaults");
}
