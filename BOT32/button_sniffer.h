/*
 * Button / state sniffer — v2.8.0, extended v2.9.0
 *
 * Passive listener for four cluster-bus broadcasts confirmed via SavvyCAN:
 *
 *   - Hand brake (KOMBI_01 0x30B byte[2] bit 7)               — v2.8.0
 *       0x80 = engaged (ON), 0x00 = released (OFF)
 *       Confirmed via HB5x.csv: 10 transitions for 5 pulls.
 *
 *   - MFSW OK button (0x5BF byte[0])               — v2.8.0 / décodage corrigé v2.10.0
 *       0x00 = repos (released), non-zéro = pressé (codes 0x01 / 0x02 observés)
 *       OK3X.csv : le repos est tout-à-zéro ; un appui met byte[0] != 0.
 *
 *   - Hazard button (Blinkmodi_01 0x366 byte[2] bit 4 = 0x10) — v2.9.0
 *       0x10 = Hazard ON, 0x00 = Hazard OFF
 *
 *   - Traction Control button (ESP_21 0x0FD byte[6])          — v2.9.0
 *       0x03 = TC button held (pressed), 0x00 = TC enabled (released)
 *
 * All exposed in the Live Data grid only (PC + mobile).
 * No firmware action is taken on the values — purely informational.
 */
#ifndef BOT32_BUTTON_SNIFFER_H
#define BOT32_BUTTON_SNIFFER_H

#include <Arduino.h>

// Register a listener on CAN_CLUSTER for 0x30B, 0x5BF, 0x366, 0x0FD.
// Call AFTER can_init().
void button_sniffer_init();

// Hand brake state — true = engaged (pulled up), false = released.
// Returns false if no frame received yet.
bool     button_sniffer_handbrake_active();
uint32_t button_sniffer_handbrake_age_ms();

// v4.7.0 — number of OK presses (released -> pressed transitions) since boot,
// counted frame by frame. Used by ok_clear (a new edge starts the 6 s hold timer).
uint32_t button_sniffer_ok_press_events();
// byte[0] of the 0x5BF frame at the last press (diagnostic: which button code).
uint8_t  button_sniffer_ok_last_code();

// v4.8.0 — OK hold diagnostics: what the 0x5BF frame does during a press.
struct OkDiag {
  uint32_t frames;        // 0x5BF frames since boot
  uint32_t gap_ms;        // interval between the last two 0x5BF frames
  uint32_t press_len_ms;  // current press so far, or the last completed press (short releases included)
  uint32_t press_frames;  // frames during that press
  uint32_t press_gap_ms;  // longest interval between frames during that press
  uint8_t  press_codes;   // OR of the byte[0] codes seen during that press
  uint32_t press_blips;   // releases shorter than OK_RELEASE_DEBOUNCE_MS during that press
};
void button_sniffer_ok_diag(OkDiag& d);

// v4.8.0 — ignition (Kl15) from Klemmen_Status_01 0x3C0 byte[2] bit 1.
// false until the first frame; check the age for freshness.
bool     button_sniffer_ignition_on();
uint32_t button_sniffer_ignition_age_ms();

// MFSW OK button — true = pressed, false = released.
// Returns false if no frame received yet.
bool     button_sniffer_ok_pressed();
uint32_t button_sniffer_ok_age_ms();

// v2.9.0 — Hazard switch — true = ON (lights flashing), false = OFF.
bool     button_sniffer_hazard_active();
uint32_t button_sniffer_hazard_age_ms();

// v2.9.0 — Traction Control button — true = pressed (TC disable held),
// false = released (TC enabled, normal state).
bool     button_sniffer_tc_pressed();
uint32_t button_sniffer_tc_age_ms();

#endif // BOT32_BUTTON_SNIFFER_H
