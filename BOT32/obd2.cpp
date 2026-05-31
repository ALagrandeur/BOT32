/*
 * OBD2 / UDS — implementation (v2.1: multi-DID polling + diagnostic actions).
 */
#include "obd2.h"
#include "config.h"
#include "settings.h"
#include "serial_proto.h"

// =============================================================
//  Cached read values
// =============================================================

// MAP
static float    last_map_mbar    = -1.0f;
static uint32_t last_map_ms      = 0;

// Ethanol content (raw 0..255 -> percent via *100/255)
static int16_t  last_ethanol_raw = -1;
static uint32_t last_ethanol_ms  = 0;

// Haldex degree of blockage (16-bit raw)
static int32_t  last_haldex_blockage_raw = -1;
static uint32_t last_haldex_blockage_ms  = 0;

// v2.8.0 — three temperature readings
// We use float with -1000.0 sentinel (instead of -1) because real temps can
// legitimately be negative (cold start at -30 C, etc.).
static float    last_dsg_oil_c     = -1000.0f;
static uint32_t last_dsg_oil_ms    = 0;
static float    last_egt_c         = -1000.0f;
static uint32_t last_egt_ms        = 0;
static float    last_engine_oil_c  = -1000.0f;
static uint32_t last_engine_oil_ms = 0;

// Generic polling state
static uint32_t last_query_ms = 0;
static uint8_t  poll_slot     = 0;   // round-robin index across enabled DIDs

// v2.8.0 — round-robin slot count: MAP, Ethanol, Haldex, DSG oil, EGT, Engine oil
static const uint8_t N_POLL_SLOTS = 6;

// Stats
static uint32_t queries_sent      = 0;
static uint32_t responses_ok      = 0;
static uint32_t responses_garbled = 0;

// =============================================================
//  Clear-DTC-all-modules state machine
// =============================================================

// ECU addresses observed in caf.csv capture (14 ECUs cleared by the OEM tool)
// Each is a UDS REQUEST address; response = request + 0x6A (for 0x71x range)
// or request + 0x08 (for 0x7E0/7E1 standard pair).
static const uint16_t CLEAR_DTC_ECUS[] = {
  0x7E0,  // Engine
  0x7E1,  // Transmission (DSG)
  0x713,  // Gateway
  0x70F,  // Haldex AWD
  0x70E, 0x710, 0x712, 0x714, 0x715,
  0x730, 0x731, 0x732,
  0x74A, 0x754,
};
static const uint8_t N_CLEAR_DTC_ECUS = sizeof(CLEAR_DTC_ECUS) / sizeof(CLEAR_DTC_ECUS[0]);

enum ClearStep {
  CS_IDLE = 0,
  CS_SESSION_EXTENDED,   // send [02 10 03]
  CS_CLEAR_DTC,          // send [04 14 FF FF FF]
  CS_SESSION_DEFAULT,    // send [02 10 01]
  CS_DONE,
};

static ClearStep g_clear_step       = CS_IDLE;
static uint8_t   g_clear_ecu_idx    = 0;
static uint32_t  g_clear_step_ms    = 0;
static const uint32_t CLEAR_STEP_DELAY_MS = 80;   // per-step delay

// =============================================================
//  Helpers
// =============================================================

// Build + send a UDS frame to a specific ECU. Payload is the UDS-level bytes
// (first byte = ISO-TP length). Pads remainder with zeros.
static bool send_uds_raw(uint16_t ecu_req_id, const uint8_t* payload, uint8_t payload_len) {
  CanFrame f;
  f.id  = ecu_req_id;
  f.len = 8;
  for (uint8_t i = 0; i < 8; i++) {
    f.data[i] = (i < payload_len) ? payload[i] : 0x00;
  }
  bool ok = can_send(CAN_OBD2, f);
  if (ok) {
    queries_sent++;
    serial_proto_report_tx(CAN_OBD2, f);
  }
  return ok;
}

// =============================================================
//  RX callback — dispatches UDS responses to the right cache
// =============================================================
static void on_obd2_rx(CanChannel ch, const CanFrame& f) {
  if (ch != CAN_OBD2) return;
  if (f.len < 4) return;

  const Settings& s = settings_get();
  uint8_t sid = f.data[1];

  // Negative response?
  if (sid == 0x7F) {
    if (f.len >= 4) {
      Serial.print("[OBD2] NRC src=0x");
      Serial.print(f.id, HEX);
      Serial.print(" SID=0x");
      Serial.print(f.data[2], HEX);
      Serial.print(" NRC=0x");
      Serial.println(f.data[3], HEX);
    }
    responses_garbled++;
    return;
  }

  // Positive ReadDataByIdentifier response (0x62 = 0x22 + 0x40)
  if (sid == 0x62 && f.len >= 6) {
    uint16_t did = ((uint16_t)f.data[2] << 8) | f.data[3];

    // === MAP (engine ECU) ===
    if (f.id == s.obd2_resp_id && did == s.obd2_did_map) {
      uint16_t raw = ((uint16_t)f.data[4] << 8) | f.data[5];
      last_map_mbar = (float)raw;
      last_map_ms   = millis();
      responses_ok++;
      return;
    }

    // === Ethanol content (engine ECU, 1-byte value) ===
    if (f.id == s.obd2_resp_id && did == UDS_DID_ETHANOL) {
      last_ethanol_raw = f.data[4];
      last_ethanol_ms  = millis();
      responses_ok++;
      return;
    }

    // === Haldex blockage (Haldex ECU, 16-bit value) ===
    if (f.id == CAN_ID_HALDEX_RESP && did == UDS_DID_HALDEX_BLOCKAGE) {
      // Response length is 0x05: SID(1) + DID(2) + value(2)
      uint16_t raw = ((uint16_t)f.data[4] << 8) | f.data[5];
      last_haldex_blockage_raw = (int32_t)raw;
      last_haldex_blockage_ms  = millis();
      responses_ok++;
      return;
    }

    // === DSG oil temp (Transmission ECU 0x7E9, DID 0x2104) — v2.8.0 ===
    //   Formula: temp_C = data[4] (validated trans 71.csv: 0x47=71)
    if (f.id == CAN_ID_TCM_RESP && did == UDS_DID_DSG_OIL) {
      last_dsg_oil_c  = (float)f.data[4];
      last_dsg_oil_ms = millis();
      responses_ok++;
      return;
    }

    // === EGT (Engine ECU 0x7E8, DID 0x40D5) — v2.8.0 ===
    //   Formula: temp_C = ((data[4]<<8)|data[5]) - 250
    //   Validated oil 72 et exhaust 480.csv: 0x02DA=730 -> 480 C
    if (f.id == s.obd2_resp_id && did == UDS_DID_EGT) {
      uint16_t raw = ((uint16_t)f.data[4] << 8) | f.data[5];
      last_egt_c   = (float)((int32_t)raw - 250);
      last_egt_ms  = millis();
      responses_ok++;
      return;
    }

    // === Engine oil temp (Engine ECU 0x7E8, DID 0xF43C) — v2.8.0 ===
    //   Formula (chosen hypothesis): temp_C = data[5] - 8
    //   Validated oil 72 et exhaust 480.csv at ~72 C reading.
    if (f.id == s.obd2_resp_id && did == UDS_DID_ENGINE_OIL) {
      last_engine_oil_c  = (float)((int16_t)f.data[5] - 8);
      last_engine_oil_ms = millis();
      responses_ok++;
      return;
    }
  }

  // Positive response to OBD-II Mode 04 (clear DTC)
  // SID_PR = 0x44 (= 0x04 + 0x40)
  if (sid == 0x44) {
    Serial.print("[OBD2] ClearDTC ack from 0x");
    Serial.println(f.id, HEX);
    return;
  }

  // Positive response to DiagnosticSessionControl (SID 0x50 = 0x10 + 0x40)
  // (no specific action — just acknowledge in clear-all state machine)
  if (sid == 0x50) {
    // session change confirmed
    return;
  }

  // Positive ack to ClearDTC (SID 0x54 = 0x14 + 0x40)
  if (sid == 0x54) {
    Serial.print("[OBD2] UDS ClearDTC ack from 0x");
    Serial.println(f.id, HEX);
    return;
  }
}

// =============================================================
//  Public API — lifecycle
// =============================================================
void obd2_init() {
  can_register_listener(CAN_OBD2, on_obd2_rx);
}

bool obd2_send_uds_read(uint16_t did, uint16_t ecu_req_id) {
  uint8_t payload[8] = {
    0x03,                                  // length = 3
    0x22,                                  // SID = ReadDataByIdentifier
    (uint8_t)((did >> 8) & 0xFF),
    (uint8_t)(did & 0xFF),
    0, 0, 0, 0
  };
  return send_uds_raw(ecu_req_id, payload, 8);
}

bool obd2_send_uds_query(uint16_t did) {
  return obd2_send_uds_read(did, settings_get().obd2_req_id);
}

// =============================================================
//  Cached value accessors
// =============================================================
float    obd2_get_last_map_mbar()    { return last_map_mbar; }

uint32_t obd2_get_map_age_ms() {
  if (last_map_ms == 0) return UINT32_MAX;
  return millis() - last_map_ms;
}

float obd2_get_last_ethanol_pct() {
  if (last_ethanol_raw < 0) return -1.0f;
  return (float)last_ethanol_raw * 100.0f / 255.0f;
}

uint32_t obd2_get_ethanol_age_ms() {
  if (last_ethanol_ms == 0) return UINT32_MAX;
  return millis() - last_ethanol_ms;
}

float obd2_get_last_haldex_blockage_pct() {
  if (last_haldex_blockage_raw < 0) return -1.0f;
  // Linear scale: 0..65535 -> 0..100% (most typical for 16-bit MQB percentage)
  return (float)last_haldex_blockage_raw * 100.0f / 65535.0f;
}

uint16_t obd2_get_last_haldex_blockage_raw() {
  return (last_haldex_blockage_raw < 0) ? 0 : (uint16_t)last_haldex_blockage_raw;
}

uint32_t obd2_get_haldex_blockage_age_ms() {
  if (last_haldex_blockage_ms == 0) return UINT32_MAX;
  return millis() - last_haldex_blockage_ms;
}

// v2.8.0 — DSG oil temp
float obd2_get_last_dsg_oil_c()       { return last_dsg_oil_c; }
uint32_t obd2_get_dsg_oil_age_ms() {
  if (last_dsg_oil_ms == 0) return UINT32_MAX;
  return millis() - last_dsg_oil_ms;
}

// v2.8.0 — EGT
float obd2_get_last_egt_c()           { return last_egt_c; }
uint32_t obd2_get_egt_age_ms() {
  if (last_egt_ms == 0) return UINT32_MAX;
  return millis() - last_egt_ms;
}

// v2.8.0 — Engine oil temp
float obd2_get_last_engine_oil_c()    { return last_engine_oil_c; }
uint32_t obd2_get_engine_oil_age_ms() {
  if (last_engine_oil_ms == 0) return UINT32_MAX;
  return millis() - last_engine_oil_ms;
}

// =============================================================
//  Action: Clear Engine Fault — OBD-II Mode 04 broadcast
// =============================================================
bool obd2_clear_engine_fault() {
  // Functional broadcast frame [01 04] on 0x700 -> all OBD-II compliant ECUs
  // respond with positive 0x44 acknowledging the clear.
  uint8_t payload[8] = { 0x01, UDS_OBD2_MODE_CLEAR_DTC, 0, 0, 0, 0, 0, 0 };
  CanFrame f;
  f.id  = CAN_ID_OBD2_BROADCAST;
  f.len = 8;
  for (uint8_t i = 0; i < 8; i++) f.data[i] = payload[i];
  bool ok = can_send(CAN_OBD2, f);
  if (ok) {
    serial_proto_report_tx(CAN_OBD2, f);
    Serial.println("[OBD2] Clear Engine Fault TX (Mode 04 broadcast on 0x700)");
  } else {
    Serial.println("[OBD2] Clear Engine Fault TX FAILED");
  }
  return ok;
}

// =============================================================
//  Action: Clear DTC on ALL modules — non-blocking state machine
// =============================================================
bool obd2_clear_all_dtcs() {
  if (g_clear_step != CS_IDLE && g_clear_step != CS_DONE) {
    Serial.println("[OBD2] Clear-all-DTCs already in progress");
    return false;
  }
  g_clear_step    = CS_SESSION_EXTENDED;
  g_clear_ecu_idx = 0;
  g_clear_step_ms = 0;
  Serial.print("[OBD2] Clear-all-DTCs START (will iterate ");
  Serial.print(N_CLEAR_DTC_ECUS);
  Serial.println(" ECUs)");
  return true;
}

bool obd2_clear_all_dtcs_in_progress() {
  return g_clear_step != CS_IDLE && g_clear_step != CS_DONE;
}

uint8_t obd2_clear_all_dtcs_progress_pct() {
  if (g_clear_step == CS_IDLE) return 0;
  if (g_clear_step == CS_DONE) return 100;
  return (uint8_t)((uint32_t)g_clear_ecu_idx * 100 / N_CLEAR_DTC_ECUS);
}

static char g_clear_ecu_label[8] = {0};
const char* obd2_clear_all_dtcs_current_ecu() {
  if (g_clear_step == CS_IDLE) return "";
  if (g_clear_step == CS_DONE) return "done";
  if (g_clear_ecu_idx >= N_CLEAR_DTC_ECUS) return "";
  snprintf(g_clear_ecu_label, sizeof(g_clear_ecu_label),
           "0x%03X", CLEAR_DTC_ECUS[g_clear_ecu_idx]);
  return g_clear_ecu_label;
}

// Drive the clear-all state machine forward. Called from obd2_tick().
static void clear_all_dtcs_advance(uint32_t now) {
  if (g_clear_step == CS_IDLE || g_clear_step == CS_DONE) return;
  if (now - g_clear_step_ms < CLEAR_STEP_DELAY_MS) return;
  if (g_clear_ecu_idx >= N_CLEAR_DTC_ECUS) {
    g_clear_step = CS_DONE;
    Serial.println("[OBD2] Clear-all-DTCs DONE");
    return;
  }

  uint16_t addr = CLEAR_DTC_ECUS[g_clear_ecu_idx];

  switch (g_clear_step) {
    case CS_SESSION_EXTENDED: {
      uint8_t payload[8] = { 0x02, 0x10, 0x03, 0, 0, 0, 0, 0 };
      send_uds_raw(addr, payload, 8);
      g_clear_step    = CS_CLEAR_DTC;
      g_clear_step_ms = now;
      break;
    }
    case CS_CLEAR_DTC: {
      uint8_t payload[8] = { 0x04, 0x14, 0xFF, 0xFF, 0xFF, 0, 0, 0 };
      send_uds_raw(addr, payload, 8);
      g_clear_step    = CS_SESSION_DEFAULT;
      g_clear_step_ms = now;
      break;
    }
    case CS_SESSION_DEFAULT: {
      uint8_t payload[8] = { 0x02, 0x10, 0x01, 0, 0, 0, 0, 0 };
      send_uds_raw(addr, payload, 8);
      g_clear_ecu_idx++;
      g_clear_step    = CS_SESSION_EXTENDED;   // start next ECU
      g_clear_step_ms = now;
      break;
    }
    default:
      g_clear_step = CS_IDLE;
      break;
  }
}

// =============================================================
//  Polling tick — round-robin across enabled DIDs
// =============================================================
void obd2_tick(bool active) {
  uint32_t now = millis();

  // Always advance the clear-all state machine if running
  clear_all_dtcs_advance(now);

  if (!active) return;

  const Settings& s = settings_get();
  uint32_t poll_period_ms = 1000UL / (s.obd2_poll_hz > 0 ? s.obd2_poll_hz : 5);
  if (now - last_query_ms < poll_period_ms) return;

  // Round-robin between enabled polls — v2.8.0: 6 slots
  // slot 0 = MAP            (always on if active)
  // slot 1 = Ethanol         (if s.poll_ethanol)
  // slot 2 = Haldex blockage (if s.poll_haldex_blockage)
  // slot 3 = DSG oil temp    (always on, no UI toggle — Live grid only)
  // slot 4 = EGT             (always on)
  // slot 5 = Engine oil temp (always on)
  // Tries all N_POLL_SLOTS so we don't skip a tick if only some are enabled.
  for (uint8_t attempt = 0; attempt < N_POLL_SLOTS; attempt++) {
    uint8_t slot = (poll_slot + attempt) % N_POLL_SLOTS;
    bool sent = false;
    switch (slot) {
      case 0:
        obd2_send_uds_query(s.obd2_did_map);
        sent = true;
        break;
      case 1:
        if (s.poll_ethanol) {
          obd2_send_uds_read(UDS_DID_ETHANOL, s.obd2_req_id);
          sent = true;
        }
        break;
      case 2:
        if (s.poll_haldex_blockage) {
          obd2_send_uds_read(UDS_DID_HALDEX_BLOCKAGE, CAN_ID_HALDEX_REQ);
          sent = true;
        }
        break;
      case 3:
        // v2.8.0 — DSG oil temp via TCM (0x7E1 -> 0x7E9)
        obd2_send_uds_read(UDS_DID_DSG_OIL, CAN_ID_TCM_REQ);
        sent = true;
        break;
      case 4:
        // v2.8.0 — EGT via engine ECU
        obd2_send_uds_read(UDS_DID_EGT, s.obd2_req_id);
        sent = true;
        break;
      case 5:
        // v2.8.0 — Engine oil temp via engine ECU
        obd2_send_uds_read(UDS_DID_ENGINE_OIL, s.obd2_req_id);
        sent = true;
        break;
    }
    if (sent) {
      poll_slot = (slot + 1) % N_POLL_SLOTS;
      last_query_ms = now;
      break;
    }
  }

  // Mark cached values stale if no fresh response
  if (obd2_get_map_age_ms() > MAP_STALE_TIMEOUT_MS) {
    last_map_mbar = -1.0f;
  }
  if (obd2_get_ethanol_age_ms() > ETHANOL_STALE_TIMEOUT_MS) {
    last_ethanol_raw = -1;
  }
  if (obd2_get_haldex_blockage_age_ms() > HALDEX_BLOCKAGE_STALE_TIMEOUT_MS) {
    last_haldex_blockage_raw = -1;
  }
  // v2.8.0 — temp staleness
  if (obd2_get_dsg_oil_age_ms() > DSG_OIL_STALE_TIMEOUT_MS) {
    last_dsg_oil_c = -1000.0f;
  }
  if (obd2_get_egt_age_ms() > EGT_STALE_TIMEOUT_MS) {
    last_egt_c = -1000.0f;
  }
  if (obd2_get_engine_oil_age_ms() > ENGINE_OIL_STALE_TIMEOUT_MS) {
    last_engine_oil_c = -1000.0f;
  }
}

// =============================================================
//  Diagnostic counters
// =============================================================
uint32_t obd2_get_queries_sent()      { return queries_sent; }
uint32_t obd2_get_responses_ok()      { return responses_ok; }
uint32_t obd2_get_responses_garbled() { return responses_garbled; }
