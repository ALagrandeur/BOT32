/*
 * haldex_modes.h — BOT32 main-side Haldex mode logic (v3.1.0)
 *
 * This module decides WHICH Haldex mode is active and pushes it to the
 * external MITM device (the ESP32-CAN-X2 running BOT32-HALDEX) via the
 * ESP-NOW link (haldex_link_set_mode).
 *
 * Mode sources (per user spec):
 *   - FWD  : armed by a PHYSICAL combo — Hazards ON + Traction Control OFF
 *            (detected by the existing cluster sniffers), OR by the app/USB
 *            button. It EXITS (back to STOCK) when the hazards are turned OFF.
 *   - 5050 : app/USB button only. It EXITS via the app/USB STOCK button.
 *   - STOCK: default; app/USB button.
 *
 * There is NO timed auto-revert (a timed mechanical revert was judged too
 * dangerous). Exit is always an explicit driver action.
 *
 * Telltale feedback: while a non-STOCK mode is active, the user wants the
 * hand-brake telltale to blink on the cluster. NOTE: this car has a MANUAL
 * hand brake whose telltale is driven by a hard-wired switch, NOT by a CAN
 * input frame — so blinking it via CAN is uncertain (bench-only at best).
 * This module therefore only EXPOSES the desired blink state
 * (haldex_modes_telltale_on()); it does NOT emit any guessed CAN frame by
 * default (honours the project safety rule). The actual telltale TX is a
 * separate, to-be-confirmed step (jalon 3).
 */
#ifndef BOT32_HALDEX_MODES_H
#define BOT32_HALDEX_MODES_H

#include <Arduino.h>

// Mode numbers (subset of the documented convention; only 3 are supported).
#define HALDEX_M_STOCK 0
#define HALDEX_M_FWD   1
#define HALDEX_M_5050  2

void    haldex_modes_init();

// Call every loop. Runs the Hazard+TC combo state machine and (re)sends the
// effective mode to the MITM whenever it changes.
void    haldex_modes_tick(uint32_t now);

// Manual mode request from the app / USB (0=STOCK, 1=FWD, 2=5050).
// Clears the physical-combo latch so the two sources don't fight.
// Returns true if the command was accepted + forwarded to the MITM.
bool    haldex_modes_set_manual(uint8_t mode);

// Current effective mode (0/1/2) as decided by this module.
uint8_t haldex_modes_get();

// True while the desired telltale-blink phase is "on" (for a future cluster
// TX and for UI indication). Toggles at ~0.5 s while a non-STOCK mode is
// active; always false in STOCK.
bool    haldex_modes_telltale_on();

#endif // BOT32_HALDEX_MODES_H
