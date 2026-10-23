/*
 * ok_clear — v4.8.0 (v4.7.0 used 10 quick presses)
 *
 * Steering-wheel trigger for the engine-code clear: OK HELD for 6 seconds
 * (MFSW 0x5BF, same decode as the "OK button" live cell: byte[0] != 0 = pressed)
 * starts the existing listen-only procedure obd2_clear_engine_start("bouton OK").
 * Releasing OK before 6 s cancels. One trigger per hold: OK must be released
 * before it can trigger again, and presses are ignored for 10 s after a trigger.
 */
#ifndef BOT32_OK_CLEAR_H
#define BOT32_OK_CLEAR_H

#include <Arduino.h>

#define OK_CLEAR_HOLD_MS      6000
#define OK_CLEAR_COOLDOWN_MS  10000

void ok_clear_init();

// Call every loop(), after can_poll().
void ok_clear_tick(uint32_t now);

// ms the OK button has been held so far in the current hold (0 if released).
uint32_t ok_clear_hold_ms();

#endif // BOT32_OK_CLEAR_H
