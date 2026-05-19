/*
 * VW MQB platform helpers — CRC8H2F checksum + 4-bit rolling counter.
 *
 * Port of webui/vw_mqb.py from mk7-cluster-bench-controller.
 * Original Python is itself a port of openpilot/opendbc/car/volkswagen/mqbcan.py.
 *
 * Why this matters: Klemmen_Status_01 (wake), Motor_14 (coolant), GRA_ACC_01
 * (steering buttons), Getriebe_11 (gear), etc. all carry a 1-byte custom
 * checksum at byte 0 and a 4-bit counter in the low nibble of byte 1. The
 * cluster validates both and silently drops frames that don't match.
 *
 * For BOT32 in-vehicle mode, we currently don't TX any of these (the real
 * gateway handles them). But we include the helpers in case we need them later
 * (e.g., for diagnostic emulation or recovery from gateway failure).
 */
#ifndef BOT32_VW_MQB_H
#define BOT32_VW_MQB_H

#include <Arduino.h>

// CRC8H2F lookup table (polynomial 0x2F). Populated by vw_mqb_init().
extern uint8_t MQB_CRC8H2F[256];

// Per-CAN-ID constant tables used by the MQB checksum.
// Returns pointer to a 16-byte array, or nullptr if the address is not in
// the MQB checksum scheme (i.e., no counter+CRC needed for this ID).
const uint8_t* mqb_const_for_addr(uint16_t addr);

// Initialize the CRC table. Call once from setup().
void vw_mqb_init();

// Compute the MQB custom checksum byte for the given CAN ID + 8-byte payload.
// The caller is responsible for placing the rolling counter (0..15) in the
// low nibble of payload[1] BEFORE calling this function.
// The returned byte should be placed in payload[0].
uint8_t mqb_checksum(uint16_t addr, const uint8_t* data, uint8_t len);

// Convenience: apply counter to byte 1 low nibble + compute and write CRC to
// byte 0. Modifies `payload` in place.
void mqb_apply(uint16_t addr, uint8_t* payload, uint8_t len, uint8_t counter);

// Returns true if the given CAN ID is in the MQB checksum scheme.
bool mqb_has_checksum(uint16_t addr);

#endif // BOT32_VW_MQB_H
