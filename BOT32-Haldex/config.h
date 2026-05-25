/*
 * BOT32-Haldex — compile-time configuration.
 *
 * Hardware: same as BOT32 main (ESP32 DevKit + WaveShare 2-CH CAN HAT,
 * wired via 9 Dupont jumpers). VIO jumper on HAT in 3V3 position.
 *
 * The 2 MCP2515 chips on the HAT are used as:
 *   CAN_PCM_SIDE     = MCP2515 #0 (CS0) -> SIT65HVD230 #0 -> PCM side of Haldex bus
 *   CAN_HALDEX_SIDE  = MCP2515 #1 (CS1) -> SIT65HVD230 #1 -> Haldex controller side
 *
 * In vehicle: the Haldex CAN bus is physically cut, BOT32-Haldex is inserted
 * between PCM and Haldex controller. Both halves of the cut bus connect to
 * the 2 separate H/L/G terminal blocks on the HAT.
 *
 * Termination jumpers (120R) on the HAT:
 *   CAN_PCM_SIDE bornier:    120R ON  (replaces the Haldex-end terminator that we cut off)
 *   CAN_HALDEX_SIDE bornier: 120R ON  (replaces the PCM-end terminator that we cut off)
 */
#ifndef BOT32H_CONFIG_H
#define BOT32H_CONFIG_H

#include <Arduino.h>

// =============================================================
//  Pins (identical to BOT32 main — same WaveShare HAT)
// =============================================================
#define PIN_LED_STATUS    2     // built-in LED on most ESP32 dev boards

// Shared SPI bus (VSPI default)
#define PIN_SPI_SCK      18     // -> HAT SCK
#define PIN_SPI_MISO     19     // <- HAT MISO
#define PIN_SPI_MOSI     23     // -> HAT MOSI

// CAN PCM side = MCP2515 #0 (CS0 / INT0 on the HAT)
#define PIN_PCM_CS        5     // -> HAT CS0
#define PIN_PCM_INT       4     // <- HAT INT0

// CAN Haldex side = MCP2515 #1 (CS1 / INT1 on the HAT)
#define PIN_HALDEX_CS    25     // -> HAT CS1
#define PIN_HALDEX_INT   26     // <- HAT INT1

// =============================================================
//  CAN bitrate
// =============================================================
// VW MQB Haldex sub-bus is typically 500 kbps (same as chassis CAN).
// Adjust here if your specific vehicle uses a different rate.
#define HALDEX_BUS_BITRATE  500000UL

// =============================================================
//  MCP2515 crystal — WaveShare 2-CH CAN HAT uses 16 MHz crystals
// =============================================================
#define MCP2515_CLOCK_MHZ   16

// =============================================================
//  ESP-NOW state broadcast rate (BOT32-Haldex -> BOT32 main)
// =============================================================
#define ESPNOW_STATE_INTERVAL_MS  200    // 5 Hz state broadcast

// =============================================================
//  Mode auto-revert timers (safety) — see mode_state.cpp
//  Set to 0 to disable auto-revert (manual revert via UI only).
// =============================================================
#define AUTO_REVERT_FWD_MS    10000  // BURNOUT mode auto-reverts after 10s
#define AUTO_REVERT_5050_MS   15000  // LAUNCH mode auto-reverts after 15s
#define AUTO_REVERT_OTHER_MS  30000  // 6040/7525 etc. revert after 30s
// Expert mode (5) does NOT auto-revert by default — user/MITM logic controls it.

#endif // BOT32H_CONFIG_H
