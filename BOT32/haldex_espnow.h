/*
 * haldex_espnow.h — ESP-NOW wireless link to the Haldex MITM module (X2)
 *
 * v3.2.0: ESP-NOW is the ONLY transport. The MITM device (ESP32-CAN-X2 running
 * BOT32-HALDEX) is installed near the Haldex unit and has no chassis-CAN tie
 * to BOT32 main, so all dialogue goes over ESP-NOW.
 *
 * The protocol is freshly defined here (BOT32-specific) — a small binary
 * format. See docs/haldex_integration.md for the on-the-wire spec the MITM
 * device must implement.
 */
#ifndef BOT32_HALDEX_ESPNOW_H
#define BOT32_HALDEX_ESPNOW_H

#include <Arduino.h>

// Initialize the ESP-NOW link. Called by haldex_link_init(). Registers the
// receive callback + peer. Coexists with the phone AP (both on channel 1).
// Safe to call multiple times.
void haldex_espnow_init();

// Send a "set mode" command to the peer over ESP-NOW.
// Returns true if the packet was queued for radio TX (does not imply RX
// by the peer — ESP-NOW is best-effort).
bool haldex_espnow_send_mode(uint8_t mode);

// v3.2.0: send a "set passthrough" command to the peer over ESP-NOW.
// passthrough=true => MITM transparent (safe); false => MITM armed.
bool haldex_espnow_send_passthrough(bool passthrough);

// Get BOT32's own MAC address (for display in UI so the user knows what
// to configure on the MITM ESP32 side).
String haldex_espnow_get_my_mac();

#endif // BOT32_HALDEX_ESPNOW_H
