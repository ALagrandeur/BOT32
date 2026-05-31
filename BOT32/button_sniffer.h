/*
 * Button / state sniffer — v2.8.0, extended v2.9.0
 *
 * Passive listener for four cluster-bus broadcasts confirmed via SavvyCAN:
 *
 *   - Hand brake (KOMBI_01 0x30B byte[2] bit 7)               — v2.8.0
 *       0x80 = engaged (ON), 0x00 = released (OFF)
 *       Confirmed via HB5x.csv: 10 transitions for 5 pulls.
 *
 *   - MFSW OK button (0x5BF byte[0])                          — v2.8.0
 *       0x07 = released, 0x00 = pressed
 *       Confirmed via OK3X.csv: 5 transitions for 3 press/release cycles.
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
