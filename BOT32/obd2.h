/*
 * OBD2 / UDS module — multi-DID polling + diagnostic action commands.
 *
 * v2.1 additions (vehicle-validated 2026-05-26):
 *   - Ethanol content polling (DID 0xF452 via engine ECU 0x7E0/0x7E8)
 *   - Haldex degree-of-blockage polling (DID 0x2BF3 via Haldex ECU 0x70F/0x779)
 *   - Clear Engine Fault action  (OBD-II Mode 04 broadcast on 0x700 -> [01 04])
 *   - Clear DTC on all modules action (UDS ClearDTC sequence to 14+ ECU addresses)
 *
 * Protocol: ISO 14229 (UDS) ReadDataByIdentifier (SID 0x22) for cached reads.
 *
 * Request frame layout (8 bytes ISO-TP single):
 *   [LEN, SID, DID_hi, DID_lo, 0x00, 0x00, 0x00, 0x00]
 *     - LEN = length of data (3 for read DID)
 *     - SID = 0x22 for ReadDataByIdentifier
 *
 * Response frame layout:
 *   [LEN, SID_PR, DID_hi, DID_lo, VAL_hi, VAL_lo, ...]
 *     - SID_PR = SID + 0x40 (positive response, e.g. 0x62 for read)
 *     - Or SID_NR = 0x7F (negative response), followed by SID and NRC
 *
 * Confirmed on VW MK7 Alltrack 2017 (engine ECU + Haldex AWD).
 */
#ifndef BOT32_OBD2_H
#define BOT32_OBD2_H

#include <Arduino.h>
#include "can_handler.h"

// =============================================================
//  Lifecycle
// =============================================================
void obd2_init();

// Tick — call from loop(). Sends periodic UDS queries + advances any
// pending multi-step actions (e.g. clear-all-DTCs state machine).
// Pass active=true to enable polling (paused otherwise).
void obd2_tick(bool active);

// =============================================================
//  Generic UDS request — used by all polling and action commands
// =============================================================

// Send a UDS ReadDataByIdentifier (SID 0x22) for the given DID, to a
// specific ECU request address. Default helper uses 0x7E0 (engine).
bool obd2_send_uds_read(uint16_t did, uint16_t ecu_req_id);
bool obd2_send_uds_query(uint16_t did);  // shorthand for engine ECU 0x7E0

// =============================================================
//  Cached read values
// =============================================================

// MAP (engine intake manifold absolute pressure, mbar) — v1.x
float obd2_get_last_map_mbar();
uint32_t obd2_get_map_age_ms();

// Ethanol content in fuel (percent 0..100) — v2.1
//   DID 0xF452 from engine ECU.
//   Raw is 1-byte value 0..255, scaled to percentage via *100/255.
//   Returns -1.0 if no recent data.
float obd2_get_last_ethanol_pct();
uint32_t obd2_get_ethanol_age_ms();

// Haldex degree of blockage (percent 0..100) — v2.1
//   DID 0x2BF3 from Haldex ECU (request 0x70F, response 0x779).
//   Raw is 16-bit value; 0 = coupling fully open (FWD), max = full lock.
//   Returns -1.0 if no recent data.
float obd2_get_last_haldex_blockage_pct();
uint16_t obd2_get_last_haldex_blockage_raw();
uint32_t obd2_get_haldex_blockage_age_ms();

// DSG transmission oil temperature (Celsius) — v2.8.0
//   DID 0x2104 from Transmission ECU (request 0x7E1, response 0x7E9).
//   Formula: temp_C = data[4] (direct byte value, validated from trans 71.csv).
//   Returns -1000 if no recent data (allows below-zero real temps).
float    obd2_get_last_dsg_oil_c();
uint32_t obd2_get_dsg_oil_age_ms();

// Exhaust gas temperature (Celsius) — v2.8.0
//   DID 0x40D5 from Engine ECU (request 0x7E0, response 0x7E8).
//   Formula: temp_C = ((data[4]<<8)|data[5]) - 250  (raw_BE minus 250).
//   Validated from oil72_exhaust480.csv (raw 0x02DA = 730 -> 480 C).
//   Returns -1000 if no recent data.
float    obd2_get_last_egt_c();
uint32_t obd2_get_egt_age_ms();

// Engine oil temperature (Celsius) — v2.8.0
//   DID 0xF43C from Engine ECU (request 0x7E0, response 0x7E8).
//   Formula (chosen hypothesis): temp_C = data[5] - 8.
//   Validated from oil72_exhaust480.csv at ~72 C reading.
//   Returns -1000 if no recent data.
float    obd2_get_last_engine_oil_c();
uint32_t obd2_get_engine_oil_age_ms();

// =============================================================
//  Action commands (one-shot, user-triggered from UI)
// =============================================================

// Clear Engine Fault — sends OBD-II Mode 04 broadcast on 0x700.
// This is the simple, session-free way to clear emission-related DTCs
// from all OBD-II-compliant ECUs in one shot.
// Returns true if the frame was queued.
bool obd2_clear_engine_fault();

// Clear DTCs on ALL modules — triggers a non-blocking state machine that
// iterates through 14+ known ECU addresses, sending for each:
//   1. DiagnosticSessionControl ExtendedSession  [02 10 03]
//   2. ClearDiagnosticInformation (all groups)   [04 14 FF FF FF]
//   3. DiagnosticSessionControl DefaultSession   [02 10 01]
// Returns true if the operation started (false if one was already in progress).
// Progress can be observed via obd2_clear_all_dtcs_in_progress() + obd2_clear_all_dtcs_progress_pct().
bool obd2_clear_all_dtcs();
bool obd2_clear_all_dtcs_in_progress();
uint8_t obd2_clear_all_dtcs_progress_pct();   // 0..100
const char* obd2_clear_all_dtcs_current_ecu();  // human label of current ECU step

// =============================================================
//  Diagnostic counters
// =============================================================
uint32_t obd2_get_queries_sent();
uint32_t obd2_get_responses_ok();
uint32_t obd2_get_responses_garbled();

#endif // BOT32_OBD2_H
