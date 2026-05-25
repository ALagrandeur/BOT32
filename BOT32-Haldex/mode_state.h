/*
 * mode_state.h — current Haldex mode + auto-revert timers
 *
 * Stores the active mode (0..5, matching BOT32 numbering convention) and
 * automatically reverts to STOCK (0) after a configurable timeout for the
 * race modes (FWD = 1, 5050 = 2, etc.) — safety feature so that if the
 * driver forgets to revert manually, the system goes back to OEM behavior.
 *
 * Mode numbering convention:
 *   0 = Stock     pass-through, OEM
 *   1 = FWD       front-wheel-drive (burnout pre-stage tire warm-up)
 *   2 = 5050      max lock (launch / 50/50 split)
 *   3 = 6040
 *   4 = 7525
 *   5 = Expert    no auto-revert; haldex_mitm.cpp controls behavior
 */
#ifndef BOT32H_MODE_STATE_H
#define BOT32H_MODE_STATE_H

#include <Arduino.h>

void    mode_state_init();
void    mode_state_set(uint8_t new_mode);   // 0..5, clamped, restarts revert timer
uint8_t mode_state_get();                   // current active mode

// Call from loop() — handles auto-revert to STOCK after timeout.
void    mode_state_tick();

// Human-readable name (for serial logs)
const char* mode_state_name(uint8_t mode);

#endif // BOT32H_MODE_STATE_H
