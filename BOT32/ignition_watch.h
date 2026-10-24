/*
 * ignition_watch — v4.8.0
 *
 * Makes sure the Haldex is back in STOCK at every vehicle restart. Calls
 * haldex_modes_force_stock() when:
 *   - BOT32 boots;
 *   - the ignition changes (Kl15 from Klemmen_Status_01 0x3C0, ON -> OFF and
 *     OFF -> ON);
 *   - the cluster CAN bus wakes up after >= 3 s of silence (car was asleep).
 * With the X2 v3.0.0 in the car this is the ONLY restart safety net (the X2
 * keeps its mode in RAM across a key cycle and during light sleep), and it
 * needs the Haldex link enabled. The optional X2 v3.1.0 adds a second net
 * (STOCK after 5 s without CAN).
 */
#ifndef BOT32_IGNITION_WATCH_H
#define BOT32_IGNITION_WATCH_H

#include <Arduino.h>

#define IGN_FRESH_MS        1000   // 0x3C0 is sent every 100 ms
#define IGN_BUS_ASLEEP_MS   3000   // no cluster frame for this long = bus asleep

void ignition_watch_init();
void ignition_watch_tick(uint32_t now);

#endif // BOT32_IGNITION_WATCH_H
