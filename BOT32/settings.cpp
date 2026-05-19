/*
 * Settings persistence — implementation using ESP32 Preferences (NVS).
 */
#include "settings.h"
#include "config.h"
#include <Preferences.h>

static Preferences prefs;
static Settings current;

#define NVS_NAMESPACE  "bot32"
#define SETTINGS_VERSION 3   // bumped when new fields added (force_tx_always, block_airbag, bench_*)

static Settings make_defaults() {
  Settings s;
  s.map_min_mbar      = MAP_MIN_MBAR_DEFAULT;
  s.map_max_mbar      = MAP_MAX_MBAR_DEFAULT;
  s.scale             = 1.0f;
  s.offset_c          = 0.0f;
  s.obd2_req_id       = CAN_ID_OBD2_REQ;
  s.obd2_resp_id      = CAN_ID_OBD2_RESP;
  s.obd2_did_map      = UDS_DID_MAP;
  s.obd2_poll_hz      = 1000 / OBD2_POLL_INTERVAL_MS;
  s.tx_rate_hz        = 1000 / MOTOR_09_TX_INTERVAL_MS;
  s.cluster_motor09_id = CAN_ID_MOTOR_09;
  s.cluster_wba03_id   = CAN_ID_WBA_03;
  s.tx_enabled        = true;
  s.listen_only_boot  = true;
  s.force_tx_always   = false;   // default: only TX in BOOST mode (S/M/N lever)
  s.block_airbag      = true;    // default: airbag IDs blocked (SAFETY)
  s.bench_test_enabled = false;  // default: bench mode OFF
  s.bench_rpm          = 1500;
  s.bench_map_mbar     = 1500;
  s.bench_test_bus     = 0;      // default: CAN_CLUSTER
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
  current.scale             = prefs.getFloat("scale", 1.0f);
  current.offset_c          = prefs.getFloat("offset_c", 0.0f);
  current.obd2_req_id       = prefs.getUShort("obd_req", CAN_ID_OBD2_REQ);
  current.obd2_resp_id      = prefs.getUShort("obd_resp", CAN_ID_OBD2_RESP);
  current.obd2_did_map      = prefs.getUShort("obd_did", UDS_DID_MAP);
  current.obd2_poll_hz      = prefs.getUShort("obd_hz", 1000 / OBD2_POLL_INTERVAL_MS);
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
  current.version           = SETTINGS_VERSION;
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
bool settings_set_scale(float v) {
  current.scale = v;
  return save_float("scale", v);
}
bool settings_set_offset_c(float v) {
  current.offset_c = v;
  return save_float("offset_c", v);
}
bool settings_set_obd2_did_map(uint16_t v) {
  current.obd2_did_map = v;
  return save_ushort("obd_did", v);
}
bool settings_set_obd2_poll_hz(uint16_t v) {
  current.obd2_poll_hz = v;
  return save_ushort("obd_hz", v);
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

void settings_reset_to_defaults() {
  current = make_defaults();
  prefs.putFloat("map_min", current.map_min_mbar);
  prefs.putFloat("map_max", current.map_max_mbar);
  prefs.putFloat("scale", current.scale);
  prefs.putFloat("offset_c", current.offset_c);
  prefs.putUShort("obd_req", current.obd2_req_id);
  prefs.putUShort("obd_resp", current.obd2_resp_id);
  prefs.putUShort("obd_did", current.obd2_did_map);
  prefs.putUShort("obd_hz", current.obd2_poll_hz);
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
  prefs.putUChar("version", current.version);
  Serial.println("[NVS] Settings reset to defaults");
}
