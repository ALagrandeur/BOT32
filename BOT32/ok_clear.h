/*
 * ok_clear — v4.7.0
 *
 * Steering-wheel trigger for the engine-code clear: OK pressed 10 times within
 * 6 seconds (MFSW 0x5BF, same decode as the "OK button" live cell) starts the
 * existing listen-only procedure obd2_clear_engine_start("bouton OK").
 * After a trigger, presses are ignored for 10 s and the count restarts from 0.
 */
#ifndef BOT32_OK_CLEAR_H
#define BOT32_OK_CLEAR_H

#include <Arduino.h>

#define OK_CLEAR_PRESSES      10
#define OK_CLEAR_WINDOW_MS    6000
#define OK_CLEAR_COOLDOWN_MS  10000

void ok_clear_init();

// Call every loop(), after can_poll().
void ok_clear_tick(uint32_t now);

// Presses currently counted inside the 6 s window (for diagnostics).
uint8_t ok_clear_presses_in_window();

#endif // BOT32_OK_CLEAR_H
