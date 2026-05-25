/*
 * espnow_peer.h — MITM side of the BOT32 ESP-NOW protocol
 *
 * Implements the peer counterpart of BOT32 main's haldex_espnow module.
 * - Listens for SET_MODE packets (0x02) and updates the active mode via
 *   the mode_state module.
 * - Periodically sends STATE packets (0x01) with current mode + pump %
 *   + target % + vehicle speed + pedal % so BOT32 main's web UI shows
 *   live data.
 *
 * Protocol (BOT32-specific, fully documented in BOT32 repo
 * docs/haldex_integration.md):
 *   Magic header:  0xBA 0xB0
 *   Type 0x01 STATE     : 10 bytes  [BA B0 01 mode pump% target% kmh pedal% 0 0]
 *   Type 0x02 SET_MODE  : 4 bytes   [BA B0 02 mode]
 *
 * All code is freshly written, MIT-licensed, no copy from any external
 * project. The on-the-wire protocol was designed for BOT32.
 */
#ifndef BOT32H_ESPNOW_PEER_H
#define BOT32H_ESPNOW_PEER_H

#include <Arduino.h>

// Initialize ESP-NOW (WiFi STA mode, register callbacks, add peer).
// Reads BOT32_MAC_STR from config — if blank, uses broadcast.
void espnow_peer_init();

// Send a STATE packet to BOT32 main. Call periodically (e.g., every 200 ms)
// from the main loop. Reads current mode + bus data via getters.
void espnow_peer_send_state();

// Periodic tick — handles the timed STATE broadcast based on
// ESPNOW_STATE_INTERVAL_MS in config. Call from loop() every iteration.
void espnow_peer_tick();

// Get this device's MAC address (for diagnostics).
String espnow_peer_get_my_mac();

#endif // BOT32H_ESPNOW_PEER_H
