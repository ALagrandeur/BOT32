/*
 * haldex_link.cpp — implementation
 *
 * All code in this file is freshly written for BOT32 (MIT-licensed). It does
 * NOT contain any source code copied from OpenHaldex-C6 (FASL-licensed).
 * Only the publicly documented CAN protocol facts (broadcast ID, command
 * frame layout, mode numbering) are used here.
 *
 * Original protocol design: Forbes Automotive — OpenHaldex-C6 project.
 * https://github.com/Forbes-Automotive/OpenHaldex-C6
 */
#include "haldex_link.h"
#include "haldex_espnow.h"
#include "settings.h"
#include "config.h"
#include "serial_proto.h"

// =============================================================
//  Internal state
// =============================================================
static HaldexState g_state;

// =============================================================
//  Public API
// =============================================================
void haldex_link_init() {
  // Zero-init state
  g_state.valid      = false;
  g_state.last_rx_ms = 0;
  g_state.len        = 0;
  for (uint8_t i = 0; i < 8; i++) g_state.raw[i] = 0;
  g_state.pump_engagement_pct = 0;
  g_state.lock_target_pct     = 0;
  g_state.vehicle_kmh         = 0;
  g_state.current_mode        = 0;
  g_state.pedal_pct           = 0;
  g_state.passthrough         = 1;   // assume safe until the X2 reports otherwise

  // v3.1.0+: ESP-NOW is the ONLY transport.
  haldex_espnow_init();
}

bool haldex_link_set_mode(uint8_t mode) {
  const Settings& s = settings_get();
  if (!s.haldex_enabled) return false;
  if (mode > 2) return false;   // sanity bound (STOCK/FWD/5050)

  bool ok = haldex_espnow_send_mode(mode);
  if (ok) {
    Serial.print("[haldex/espnow] sent set_mode=");
    Serial.println(mode);
  } else {
    Serial.println("[haldex/espnow] set_mode TX failed");
  }
  return ok;
}

bool haldex_link_set_passthrough(bool passthrough) {
  const Settings& s = settings_get();
  if (!s.haldex_enabled) return false;
  bool ok = haldex_espnow_send_passthrough(passthrough);
  if (ok) {
    Serial.print("[haldex/espnow] sent passthrough=");
    Serial.println(passthrough ? "ON" : "OFF");
  } else {
    Serial.println("[haldex/espnow] passthrough TX failed");
  }
  return ok;
}

// Update state from an alternative transport (called by haldex_espnow.cpp
// when a STATE packet arrives over ESP-NOW). Also usable by future
// transports (UART, etc.) without changing call sites.
void haldex_link_update_state(const HaldexState& new_state) {
  g_state = new_state;
}

HaldexState haldex_link_get_state() {
  return g_state;  // shallow copy
}

uint32_t haldex_link_get_age_ms() {
  if (!g_state.valid) return UINT32_MAX;
  return millis() - g_state.last_rx_ms;
}

const char* haldex_mode_name(uint8_t mode) {
  switch (mode) {
    case 0: return "Stock";
    case 1: return "FWD";
    case 2: return "5050";
    default: return "?";
  }
}
