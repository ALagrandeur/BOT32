/*
 * haldex_espnow.h — ESP-NOW wireless transport for the Haldex link
 *
 * Alternative to the CAN-based transport in haldex_link.cpp. Used when the
 * MITM ESP32 cannot share a CAN bus with BOT32 (e.g., MITM is installed
 * near the rear axle and only has its 2 Haldex CAN modules — no chassis
 * CAN connection).
 *
 * Selected via settings.haldex_transport = 1.
 *
 * The protocol over ESP-NOW is freshly defined here (BOT32-specific). It is
 * NOT the same as any external project's protocol — it's a small binary
 * format optimized for this use case. See docs/haldex_integration.md for
 * the on-the-wire spec the MITM device must implement.
 */
#ifndef BOT32_HALDEX_ESPNOW_H
#define BOT32_HALDEX_ESPNOW_H

#include <Arduino.h>

// Initialize the ESP-NOW transport. Called by haldex_link_init() when the
// haldex_transport setting is set to 1. Sets WiFi to STA mode (no AP, no
// network association) and registers the receive callback. Safe to call
// multiple times.
void haldex_espnow_init();

// Send a "set mode" command to the peer over ESP-NOW.
// Returns true if the packet was queued for radio TX (does not imply RX
// by the peer — ESP-NOW is best-effort).
bool haldex_espnow_send_mode(uint8_t mode);

// Get BOT32's own MAC address (for display in UI so the user knows what
// to configure on the MITM ESP32 side).
String haldex_espnow_get_my_mac();

#endif // BOT32_HALDEX_ESPNOW_H
