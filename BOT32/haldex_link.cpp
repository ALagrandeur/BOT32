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

// Default byte positions used to extract parsed fields from the broadcast
// payload. These can be overridden by the user via settings if the MITM
// module they run uses a different layout.
// Defaults follow the public protocol documentation (positions are
// 0-indexed offsets into the 8-byte payload).
static const uint8_t DEFAULT_POS_ENGAGEMENT = 1;
static const uint8_t DEFAULT_POS_TARGET     = 2;
static const uint8_t DEFAULT_POS_SPEED      = 3;
static const uint8_t DEFAULT_POS_MODE       = 5;
static const uint8_t DEFAULT_POS_PEDAL      = 6;

// =============================================================
//  CAN listener — fires on every received frame, filters internally
// =============================================================
static void on_haldex_can_rx(CanChannel ch, const CanFrame& f) {
  const Settings& s = settings_get();
  if (!s.haldex_enabled) return;

  // Channel filter: respect user choice of which bus the MITM module sits on
  CanChannel target_ch = (s.haldex_bus == 1) ? CAN_OBD2 : CAN_CLUSTER;
  if (ch != target_ch) return;

  // ID filter: must match the configured state-broadcast ID
  if (f.id != s.haldex_state_id) return;
  if (f.len == 0 || f.len > 8) return;

  // Capture raw bytes
  g_state.valid      = true;
  g_state.last_rx_ms = f.timestamp;
  g_state.len        = f.len;
  for (uint8_t i = 0; i < 8; i++) {
    g_state.raw[i] = (i < f.len) ? f.data[i] : 0;
  }

  // Parse fields at configurable positions (with bounds check)
  if (DEFAULT_POS_ENGAGEMENT < f.len) g_state.pump_engagement_pct = f.data[DEFAULT_POS_ENGAGEMENT];
  if (DEFAULT_POS_TARGET     < f.len) g_state.lock_target_pct     = f.data[DEFAULT_POS_TARGET];
  if (DEFAULT_POS_SPEED      < f.len) g_state.vehicle_kmh         = f.data[DEFAULT_POS_SPEED];
  if (DEFAULT_POS_MODE       < f.len) g_state.current_mode        = f.data[DEFAULT_POS_MODE];
  if (DEFAULT_POS_PEDAL      < f.len) g_state.pedal_pct           = f.data[DEFAULT_POS_PEDAL];
}

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

  // v3.1.0: ESP-NOW is now the ONLY transport (CAN transport removed from UI).
  // The legacy CAN listener (on_haldex_can_rx) is retained in this file for
  // reference but is no longer registered.
  (void)on_haldex_can_rx;
  haldex_espnow_init();
}

bool haldex_link_set_mode(uint8_t mode) {
  const Settings& s = settings_get();
  if (!s.haldex_enabled) return false;
  if (mode > 5) return false;   // sanity bound

  // v3.1.0: ESP-NOW only.
  bool ok = haldex_espnow_send_mode(mode);
  if (ok) {
    Serial.print("[haldex/espnow] sent set_mode=");
    Serial.println(mode);
  } else {
    Serial.println("[haldex/espnow] set_mode TX failed");
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
    case 3: return "6040";
    case 4: return "7525";
    case 5: return "Expert";
    default: return "?";
  }
}
