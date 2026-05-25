/*
 * Coolant byte / temperature mapping helpers.
 *
 * Cluster 5G1 920 740B uses Motor_09 byte 0 as coolant:
 *   temp_C = byte * 0.7339 - 43.94    (validated empirically)
 *   0x80 (128) = 50.0 C
 *   0xED (237) = 130.0 C
 *
 * Mapping = LINEAR proportional across the full temperature range.
 * (v1.6.0 had an optional dead-zone-aware mode; vehicle testing in v2.0
 * confirmed linear is the right model — option removed.)
 */
#ifndef BOT32_COOLANT_H
#define BOT32_COOLANT_H

#include <Arduino.h>
#include "can_handler.h"

// Convert byte 0 to temperature (Celsius)
float coolant_byte_to_temp_c(uint8_t b);

// Convert temperature (Celsius) to byte 0
uint8_t coolant_temp_c_to_byte(float temp_c);

// Map MAP pressure (mbar) to coolant byte (linear).
//
// - map_mbar: current MAP value (mbar absolute)
// - map_min_mbar: MAP at idle (-> displays as 50 C, low needle)
// - map_max_mbar: MAP at full boost (-> displays as 130 C, red zone)
//
// Returns: byte 0 to write into Motor_09 payload.
uint8_t coolant_map_mbar_to_byte(
  float map_mbar,
  float map_min_mbar,
  float map_max_mbar
);

// Build a complete Motor_09 payload (8 bytes) into `out`.
// out[0] = coolant byte, out[1..7] = magic tail from r00li.
void coolant_build_motor_09(uint8_t coolant_byte, uint8_t* out);

// =============================================================
//  Real coolant SNIFFER — listens to Motor_09 on the cluster bus (CAN0)
//  and decodes byte 0 to get the real engine coolant temperature being
//  broadcast by the gateway/engine ECU.
//
//  In listen-only mode (tx_enabled = OFF), this shows the REAL engine
//  coolant. When we TX our override, our own frame doesn't echo back
//  to RX (MCP2515 in normal mode), so the sniff value freezes to the
//  last value the real ECU broadcast before we started overriding.
// =============================================================

// Initialize the sniffer (registers a CAN listener for Motor_09 on CAN0).
// Call AFTER can_init() in setup().
void coolant_sniffer_init();

// Get the last sniffed real coolant temperature in Celsius, or -1.0f if
// no frame has been received yet (CAN0 not connected or no traffic).
float coolant_get_real_temp_c();

// Get the last sniffed coolant byte raw value (0..255), or 0 if none.
uint8_t coolant_get_real_byte();

// Get age in ms since last sniff. UINT32_MAX if never received.
uint32_t coolant_get_real_age_ms();

#endif // BOT32_COOLANT_H
