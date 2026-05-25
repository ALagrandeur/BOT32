/*
 * can_handler.h — 2x MCP2515 wrapper for the Haldex MITM
 *
 * Drives both halves of the cut Haldex bus:
 *   PCM side    : MCP2515 #0  -> SIT65HVD230 #0  -> twisted pair towards PCM
 *   Haldex side : MCP2515 #1  -> SIT65HVD230 #1  -> twisted pair towards Haldex ECU
 *
 * Same hardware and pinout as BOT32 main (WaveShare 2-CH CAN HAT on shared SPI).
 *
 * Required Arduino library: ACAN2515 by Pierre Molinaro (Library Manager).
 */
#ifndef BOT32H_CAN_HANDLER_H
#define BOT32H_CAN_HANDLER_H

#include <Arduino.h>

// Generic CAN frame used by both buses (we only handle standard 11-bit IDs
// for the Haldex MITM use case).
struct CanFrame {
  uint32_t id;
  uint8_t  len;
  uint8_t  data[8];
};

// Initialize both MCP2515 chips on the shared SPI bus.
// Returns true if both came up OK.
bool can_init();

// =============================================================
//  PCM side (MCP2515 #0)
// =============================================================
bool can_pcm_available();
bool can_pcm_receive(CanFrame& f);
bool can_pcm_send(const CanFrame& f);

// =============================================================
//  Haldex side (MCP2515 #1)
// =============================================================
bool can_haldex_available();
bool can_haldex_receive(CanFrame& f);
bool can_haldex_send(const CanFrame& f);

// =============================================================
//  Counters (per side)
// =============================================================
struct CanSideStats {
  uint32_t rx;
  uint32_t tx_ok;
  uint32_t tx_fail;
};
CanSideStats can_pcm_stats();
CanSideStats can_haldex_stats();

#endif // BOT32H_CAN_HANDLER_H
