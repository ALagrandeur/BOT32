/*
 * OBD2 / UDS — implementation.
 */
#include "obd2.h"
#include "config.h"
#include "settings.h"

static float    last_map_mbar = -1.0f;
static uint32_t last_map_ms   = 0;
static uint32_t last_query_ms = 0;

// Stats for diagnostics
static uint32_t queries_sent     = 0;
static uint32_t responses_ok     = 0;
static uint32_t responses_garbled = 0;

// =============================================================
//  RX callback: parses UDS response on 0x7E8
// =============================================================
static void on_obd2_rx(CanChannel ch, const CanFrame& f) {
  if (ch != CAN_OBD2) return;
  const Settings& s = settings_get();
  if (f.id != s.obd2_resp_id) return;   // user-configurable response ID
  if (f.len < 6) {
    responses_garbled++;
    return;
  }

  // Expected: [LEN, 0x62, DID_hi, DID_lo, VAL_hi, VAL_lo, ...]
  uint8_t sid    = f.data[1];
  uint8_t did_hi = f.data[2];
  uint8_t did_lo = f.data[3];

  if (sid != 0x62) {
    if (sid == 0x7F) {
      Serial.print("[OBD2] NRC: SID=0x");
      Serial.print(f.data[2], HEX);
      Serial.print(" NRC=0x");
      Serial.println(f.data[3], HEX);
    }
    responses_garbled++;
    return;
  }

  uint16_t did = (did_hi << 8) | did_lo;
  if (did != s.obd2_did_map) {
    // Response to a different DID — ignore
    return;
  }

  uint16_t map_raw = (f.data[4] << 8) | f.data[5];
  last_map_mbar = (float)map_raw;
  last_map_ms   = millis();
  responses_ok++;
}

// =============================================================
//  Public API
// =============================================================
void obd2_init() {
  can_register_listener(CAN_OBD2, on_obd2_rx);
}

bool obd2_send_uds_query(uint16_t did) {
  CanFrame f;
  f.id  = settings_get().obd2_req_id;  // user-configurable request ID
  f.len = 8;
  f.data[0] = 0x03;                      // ISO-TP single frame, 3 data bytes
  f.data[1] = 0x22;                      // SID = ReadDataByIdentifier
  f.data[2] = (did >> 8) & 0xFF;
  f.data[3] = did & 0xFF;
  f.data[4] = 0x00;
  f.data[5] = 0x00;
  f.data[6] = 0x00;
  f.data[7] = 0x00;
  bool ok = can_send(CAN_OBD2, f);
  if (ok) queries_sent++;
  return ok;
}

float obd2_get_last_map_mbar() {
  return last_map_mbar;
}

uint32_t obd2_get_map_age_ms() {
  if (last_map_ms == 0) return UINT32_MAX;
  return millis() - last_map_ms;
}

uint32_t obd2_get_queries_sent()      { return queries_sent; }
uint32_t obd2_get_responses_ok()      { return responses_ok; }
uint32_t obd2_get_responses_garbled() { return responses_garbled; }

void obd2_tick(bool active) {
  if (!active) return;

  uint32_t now = millis();
  const Settings& s = settings_get();
  uint32_t poll_period_ms = 1000UL / (s.obd2_poll_hz > 0 ? s.obd2_poll_hz : 5);
  if (now - last_query_ms >= poll_period_ms) {
    obd2_send_uds_query(s.obd2_did_map);   // user-configurable DID
    last_query_ms = now;
  }

  // Mark MAP as stale if no fresh response in MAP_STALE_TIMEOUT_MS
  if (obd2_get_map_age_ms() > MAP_STALE_TIMEOUT_MS) {
    last_map_mbar = -1.0f;
  }
}
