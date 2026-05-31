/*
 * Button / state sniffer — v2.8.0
 *
 * Passive listener for two cluster-bus broadcasts confirmed via SavvyCAN:
 *
 *   - Hand brake (KOMBI_01 0x30B byte[2] bit 7)
 *       0x80 = engaged (ON), 0x00 = released (OFF)
 *       Confirmed via HB5x.csv: 10 transitions for 5 pulls.
 *
 *   - MFSW OK button (0x5BF byte[0])
 *       0x07 = released, 0x00 = pressed
 *       Confirmed via OK3X.csv: 5 transitions for 3 press/release cycles.
 *
 * Per user choice (v2.8.0): these are EXPOSED in the Live Data grid only.
 * No firmware action is taken on the values — they are purely informational
 * for now. Future versions may bind them to actions (Clear DTC combo, etc.).
 */
#ifndef BOT32_BUTTON_SNIFFER_H
#define BOT32_BUTTON_SNIFFER_H

#include <Arduino.h>

// Register a listener on CAN_CLUSTER for 0x30B and 0x5BF.
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

#endif // BOT32_BUTTON_SNIFFER_H
