/*
 * OBD2 / UDS — implementation.
 */
#include "obd2.h"
#include "config.h"

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
  if (f.id != CAN_ID_OBD2_RESP) return;
  if (f.len < 6) {
    responses_garbled++;
    return;
  }

  // Expected: [LEN, 0x62, DID_hi, DID_lo, VAL_hi, VAL_lo, ...]
  // LEN = f.data[0] (ISO-TP single frame length)
  uint8_t sid    = f.data[1];
  uint8_t did_hi = f.data[2];
  uint8_t did_lo = f.data[3];

  if (sid != 0x62) {
    // Could be a negative response (0x7F SID NRC). Log but ignore.
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
  if (did != UDS_DID_MAP) {
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
  f.id  = CAN_ID_OBD2_REQ;
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

void obd2_tick(bool active) {
  if (!active) return;

  uint32_t now = millis();
  if (now - last_query_ms >= OBD2_POLL_INTERVAL_MS) {
    obd2_send_uds_query(UDS_DID_MAP);
    last_query_ms = now;
  }

  // Mark MAP as stale if no fresh response in MAP_STALE_TIMEOUT_MS
  if (obd2_get_map_age_ms() > MAP_STALE_TIMEOUT_MS) {
    last_map_mbar = -1.0f;
  }
}
