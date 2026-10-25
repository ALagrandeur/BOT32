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
// v4.7.0 — press edges counted per frame (a poll in loop() could miss a short
// press if press + release frames are drained by the same can_poll()).
static uint32_t ok_press_events = 0;
static uint8_t  ok_last_code    = 0;
// v4.8.0 — what the 0x5BF frame does during a press (OK hold diagnostics)
static uint32_t ok_frames       = 0;    // 0x5BF frames since boot
static uint32_t ok_gap_ms       = 0;    // interval between the last two 0x5BF frames
static uint32_t ok_press_start  = 0;    // first pressed frame of the current / last press
static uint32_t ok_press_len_ms = 0;    // last completed press: first pressed -> first released frame
static uint32_t ok_press_frames = 0;    // frames during the current / last press
static uint32_t ok_press_gap_ms = 0;    // longest interval between frames during that press
static uint8_t  ok_press_codes  = 0;    // OR of the byte[0] codes seen during that press
static uint32_t ok_press_blips  = 0;    // releases shorter than OK_RELEASE_DEBOUNCE_MS in that press
static uint32_t ok_release_t    = 0;    // first released frame after a press

// === Ignition Kl15 (Klemmen_Status_01 0x3C0, byte[2] bit 1) — v4.8.0 ===
static bool     kl15_on     = false;
static uint32_t kl15_ms     = 0;

// === Hazard switch (Blinkmodi_01 0x366, byte[2] bit 4 = 0x10) — v2.9.0 ===
static bool     hz_active   = false;
static uint32_t hz_ms       = 0;

// === Traction Control button (ESP_21 0x0FD, byte[6]) — v2.9.0 ===
//   0x03 = pressed (held), 0x00 = released (TC enabled, normal)
static bool     tc_pressed  = false;
static uint32_t tc_ms       = 0;

static void on_cluster_rx(CanChannel ch, const CanFrame& f) {
  if (ch != CAN_CLUSTER) return;

  if (f.id == CAN_ID_KLEMMEN_STATUS && f.len >= 3) {
    kl15_on = (f.data[2] & 0x02) != 0;
    kl15_ms = millis();
    return;
  }

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
    bool pressed_now = (f.data[0] != 0x00);
    uint32_t t = millis();
    uint32_t gap = ok_frames ? t - ok_ms : 0;
    ok_frames++;
    ok_gap_ms = gap;
    if (pressed_now && !ok_pressed) {
      ok_press_events++;
      ok_last_code = f.data[0];
      // a short release (same rule as ok_clear) continues the same press
      if (ok_press_frames && (t - ok_release_t) < OK_RELEASE_DEBOUNCE_MS) {
        ok_press_blips++;
      } else {
        ok_press_start  = t;
        ok_press_frames = 0;
        ok_press_gap_ms = 0;
        ok_press_codes  = 0;
        ok_press_blips  = 0;
      }
    }
    if (pressed_now) {
      if (ok_press_frames && gap > ok_press_gap_ms) ok_press_gap_ms = gap;
      ok_press_frames++;
      ok_press_codes |= f.data[0];
    } else if (ok_pressed) {
      ok_press_len_ms = t - ok_press_start;
      ok_release_t    = t;
    }
    ok_pressed = pressed_now;
    ok_ms      = t;
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
uint32_t button_sniffer_ok_press_events() { return ok_press_events; }
bool     button_sniffer_ignition_on()     { return kl15_on; }
uint32_t button_sniffer_ignition_age_ms() {
  if (kl15_ms == 0) return UINT32_MAX;
  return millis() - kl15_ms;
}
uint8_t  button_sniffer_ok_last_code()    { return ok_last_code; }

void button_sniffer_ok_diag(OkDiag& d) {
  d.frames       = ok_frames;
  d.gap_ms       = ok_gap_ms;
  d.press_len_ms = ok_pressed ? millis() - ok_press_start : ok_press_len_ms;
  d.press_frames = ok_press_frames;
  d.press_gap_ms = ok_press_gap_ms;
  d.press_codes  = ok_press_codes;
  d.press_blips  = ok_press_blips;
}
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
