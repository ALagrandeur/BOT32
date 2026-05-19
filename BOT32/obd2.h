/*
 * OBD2 / UDS module — periodic MAP polling via DID 0x39C0 (Saugrohrdruck).
 *
 * Protocol: ISO 14229 (UDS) ReadDataByIdentifier (SID 0x22) on tester address
 * 0x7E0, expects response on 0x7E8 (engine ECU).
 *
 * Request frame (8 bytes ISO-TP single):
 *   [0x03, 0x22, DID_hi, DID_lo, 0x00, 0x00, 0x00, 0x00]
 *   |     |     └──────┴── DID being queried (0x39C0 for MAP)
 *   |     └── SID = ReadDataByIdentifier
 *   └── Length = 3 bytes of data
 *
 * Response frame (positive):
 *   [LEN, 0x62, DID_hi, DID_lo, VAL_hi, VAL_lo, ...]
 *   |    |     └──────┴── DID echo
 *   |    └── 0x22 + 0x40 = 0x62 (positive response)
 *   └── Length
 *
 * For MAP (DID 0x39C0): value is 16-bit unsigned mbar absolute.
 *
 * Confirmed on VW MK7 Alltrack 2017 (sister project mk7-cluster-bench-controller).
 */
#ifndef BOT32_OBD2_H
#define BOT32_OBD2_H

#include <Arduino.h>
#include "can_handler.h"

// Initialize OBD2 module (registers RX listener on CAN_OBD2 channel).
void obd2_init();

// Send a UDS ReadDataByIdentifier query for the given DID.
// Returns true if queued OK on CAN_OBD2.
bool obd2_send_uds_query(uint16_t did);

// Get the last MAP reading (mbar absolute), or -1.0 if no recent data.
float obd2_get_last_map_mbar();

// Get age (ms) of the last MAP reading, or UINT32_MAX if never received.
uint32_t obd2_get_map_age_ms();

// Tick — call from loop(). Sends periodic UDS queries when in BOOST mode.
// Pass active=true to enable polling, false to pause (e.g., in SILENT mode).
void obd2_tick(bool active);

// Diagnostic counters
uint32_t obd2_get_queries_sent();
uint32_t obd2_get_responses_ok();
uint32_t obd2_get_responses_garbled();

#endif // BOT32_OBD2_H
