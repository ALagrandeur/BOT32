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
#include <ArduinoJson.h>

#define BUILD_VERSION  "0.1"
#define BUILD_DATE     __DATE__

static bool     subscribe_frames = false;     // off by default to avoid spam
static char     line_buf[512];
static uint16_t line_len = 0;
static uint32_t last_status_ms = 0;
static const uint32_t STATUS_INTERVAL_MS = 500;

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
  doc["scale"]             = s.scale;
  doc["offset_c"]          = s.offset_c;
  doc["obd2_req_id"]       = s.obd2_req_id;
  doc["obd2_resp_id"]      = s.obd2_resp_id;
  doc["obd2_did_map"]      = s.obd2_did_map;
  doc["obd2_poll_hz"]      = s.obd2_poll_hz;
  doc["tx_rate_hz"]        = s.tx_rate_hz;
  doc["cluster_motor09_id"] = s.cluster_motor09_id;
  doc["cluster_wba03_id"]   = s.cluster_wba03_id;
  doc["tx_enabled"]        = s.tx_enabled;
  doc["listen_only_boot"]  = s.listen_only_boot;
  serializeJson(doc, Serial);
  Serial.println();
}

static void emit_status() {
  JsonDocument doc;
  doc["evt"] = "status";
  doc["uptime_ms"]   = millis();
  doc["lever"]       = String(lever_get());
  doc["gear"]        = lever_get_gear();
  doc["lever_age_ms"] = lever_get_age_ms();
  float map = obd2_get_last_map_mbar();
  doc["map_mbar"]    = map >= 0 ? map : (float)-1;
  doc["map_age_ms"]  = obd2_get_map_age_ms();

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

  if (strcmp(cmd, "set") == 0) {
    const char* key = doc["key"];
    if (!key) { emit_ack("set", false, "no key"); return; }
    bool ok = false;
    if      (strcmp(key, "map_min_mbar")  == 0) ok = settings_set_map_min_mbar(doc["value"]  | 0.0f);
    else if (strcmp(key, "map_max_mbar")  == 0) ok = settings_set_map_max_mbar(doc["value"]  | 0.0f);
    else if (strcmp(key, "scale")         == 0) ok = settings_set_scale(doc["value"]         | 1.0f);
    else if (strcmp(key, "offset_c")      == 0) ok = settings_set_offset_c(doc["value"]      | 0.0f);
    else if (strcmp(key, "obd2_did_map")  == 0) ok = settings_set_obd2_did_map(doc["value"]  | 0);
    else if (strcmp(key, "obd2_poll_hz")  == 0) ok = settings_set_obd2_poll_hz(doc["value"]  | 5);
    else if (strcmp(key, "tx_rate_hz")    == 0) ok = settings_set_tx_rate_hz(doc["value"]    | 20);
    else if (strcmp(key, "tx_enabled")    == 0) ok = settings_set_tx_enabled(doc["value"]    | false);
    else { emit_ack("set", false, "unknown key"); return; }
    emit_ack("set", ok);
    if (ok) emit_settings();
    return;
  }

  emit_ack(cmd, false, "unknown cmd");
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

void serial_proto_log(const char* level, const char* msg) {
  JsonDocument doc;
  doc["evt"]   = "log";
  doc["level"] = level;
  doc["msg"]   = msg;
  serializeJson(doc, Serial);
  Serial.println();
}
