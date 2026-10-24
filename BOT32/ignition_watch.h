/*
 * ignition_watch — v4.8.0
 *
 * Puts the Haldex back in STOCK after a LONG cut only. A cut = ignition OFF
 * (Kl15 from Klemmen_Status_01 0x3C0) or the cluster CAN bus silent. A cut
 * shorter than IGN_LONG_CUT_MS (key off/on, stall + restart, CAN drop-out)
 * keeps the X2 mode. When a cut reaches IGN_LONG_CUT_MS,
 * haldex_modes_force_stock() is called right away (the X2 is still awake: it
 * only sleeps after 15 min without CAN) and again when the ignition comes back
 * if that STOCK was not confirmed.
 * A full power-up of BOT32 (battery reconnected, BOT32 plugged in or powered
 * with the key) also forces STOCK: the length of the cut is unknown. Any other
 * reset (brownout while cranking, crash) keeps the X2 mode.
 * With the X2 v3.0.0 in the car this is the ONLY restart safety net (the X2
 * keeps its mode in RAM across a key cycle and during light sleep), and it
 * needs the Haldex link enabled. The optional X2 v3.1.0 adds a second net
 * (STOCK after 5 s without CAN).
 */
#ifndef BOT32_IGNITION_WATCH_H
#define BOT32_IGNITION_WATCH_H

#include <Arduino.h>

#define IGN_FRESH_MS        1000     // 0x3C0 is sent every 100 ms
#define IGN_BUS_SILENT_MS   3000     // no cluster frame for this long = bus silent (cut)
#define IGN_LONG_CUT_MS     300000UL // 5 min: a shorter cut keeps the X2 mode

void ignition_watch_init();
void ignition_watch_tick(uint32_t now);

#endif // BOT32_IGNITION_WATCH_H
