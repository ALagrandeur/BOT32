/*
 * Gear lever decoder — decodes WBA_03 (CAN 0x394) byte 1 high nibble.
 *
 * Source: sister project mk7-cluster-bench-controller + openDBC vw_mqb.dbc.
 *
 *   byte 1 high nibble  ->  lever position
 *   ───────────────────────────────────────
 *        0x10           ->  P (Park)
 *        0x20           ->  R (Reverse)
 *        0x30           ->  N (Neutral)
 *        0x40           ->  D (Drive)
 *        0x50           ->  S (Sport)
 *        0x60           ->  M (Manual / Tiptronic)
 *
 *   byte 3 low nibble (1..6)  ->  engaged gear digit (only in D/S/M)
 *   For example: lever=S, byte3=3 -> "S3"
 *
 * BOOST mode = lever in {S, M, N}
 */
#ifndef BOT32_LEVER_DECODER_H
#define BOT32_LEVER_DECODER_H

#include <Arduino.h>
#include "can_handler.h"

// Initialize (registers RX listener on CAN_CLUSTER channel for WBA_03).
void lever_init();

// Get current lever position as single char: 'P', 'R', 'N', 'D', 'S', 'M', or '?' if unknown/stale.
char lever_get();

// Get current engaged gear digit (1..6) if applicable (D/S/M), else 0.
uint8_t lever_get_gear();

// True if lever is in BOOST mode trigger position (S/M/N).
bool lever_is_boost_mode();

// Age in ms since last WBA_03 frame received.
uint32_t lever_get_age_ms();

#endif // BOT32_LEVER_DECODER_H
