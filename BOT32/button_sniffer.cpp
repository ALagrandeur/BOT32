/*
 * Button / state sniffer — implementation (v2.8.0, extended v2.9.0).
 */
#include "button_sniffer.h"
#include "config.h"
#include "can_handler.h"

// === Hand brake (KOMBI_01 0x30B, byte[2] bit 7) — v2.8.0 ===
static bool     hb_active   = false;
static uint32_t hb_ms       = 0;

// === MFSW OK button (0x5BF, byte[0]) — v2.8.0 ===
//   raw value 0x07 = released, 0x00 = pressed
static bool     ok_pressed  = false;
static uint32_t ok_ms       = 0;

// === Hazard switch (Blinkmodi_01 0x366, byte[2] bit 4 = 0x10) — v2.9.0 ===
static bool     hz_active   = false;
static uint32_t hz_ms       = 0;

// === Traction Control button (ESP_21 0x0FD, byte[6]) — v2.9.0 ===
//   0x03 = pressed (held), 0x00 = released (TC enabled, normal)
static bool     tc_pressed  = false;
static uint32_t tc_ms       = 0;

static void on_cluster_rx(CanChannel ch, const CanFrame& f) {
  if (ch != CAN_CLUSTER) return;

  if (f.id == CAN_ID_KOMBI_01 && f.len >= 3) {
    bool now_active = (f.data[2] & 0x80) != 0;
    hb_active = now_active;
    hb_ms     = millis();
    return;
  }

  if (f.id == CAN_ID_MFSW && f.len >= 1) {
    // v2.10.0 FIX: the IDLE/rest state of 0x5BF byte[0] is 0x00 — confirmed in
    // OK3X.csv (123604 / 123772 frames are all-zero). A pressed button shows a
    // NON-ZERO code in byte[0] (0x01 and 0x02 observed; D2..D8 always 0x00).
    // The previous code tested == 0x00, so it reported "pressed" while idle.
    // Correct logic: pressed = byte[0] != 0x00.
    ok_pressed = (f.data[0] != 0x00);
    ok_ms      = millis();
    return;
  }

  // v2.9.0 — Hazard switch (Blinkmodi_01)
  // Vehicle capture confirmed: byte[2] bit 4 (mask 0x10) toggles when the
  // hazard button is pressed (0x00 -> 0x10 -> 0x00).
  if (f.id == CAN_ID_HAZARD && f.len >= 3) {
    hz_active = (f.data[2] & 0x10) != 0;
    hz_ms     = millis();
    return;
  }

  // v2.9.0 — Traction Control button (ESP_21)
  // Vehicle capture confirmed: byte[6] = 0x00 normally, 0x03 while held.
  if (f.id == CAN_ID_TC_BUTTON && f.len >= 7) {
    tc_pressed = (f.data[6] == 0x03);
    tc_ms      = millis();
    return;
  }
}

void button_sniffer_init() {
  can_register_listener(CAN_CLUSTER, on_cluster_rx);
}

bool button_sniffer_handbrake_active()    { return hb_active; }
uint32_t button_sniffer_handbrake_age_ms() {
  if (hb_ms == 0) return UINT32_MAX;
  return millis() - hb_ms;
}

bool button_sniffer_ok_pressed()         { return ok_pressed; }
uint32_t button_sniffer_ok_age_ms() {
  if (ok_ms == 0) return UINT32_MAX;
  return millis() - ok_ms;
}

// v2.9.0 — Hazard
bool button_sniffer_hazard_active()      { return hz_active; }
uint32_t button_sniffer_hazard_age_ms() {
  if (hz_ms == 0) return UINT32_MAX;
  return millis() - hz_ms;
}

// v2.9.0 — Traction Control button
bool button_sniffer_tc_pressed()         { return tc_pressed; }
uint32_t button_sniffer_tc_age_ms() {
  if (tc_ms == 0) return UINT32_MAX;
  return millis() - tc_ms;
}
