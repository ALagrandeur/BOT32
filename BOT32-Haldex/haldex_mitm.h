/*
 * haldex_mitm.h — Haldex bus MITM logic (USER-IMPLEMENTED STUBS)
 *
 * This is where the Haldex-specific knowledge lives:
 *   - Identifying the "pump demand" frame coming from the PCM
 *   - Modifying that frame's payload to force a specific pump %
 *   - Extracting live state from frames coming from the Haldex controller
 *     (current pump %, lock target %, vehicle speed, pedal %)
 *   - Handling MQB CRC + counter if the Haldex frames use them
 *
 * BOT32-Haldex provides the SKELETON — initially the functions just
 * pass-through with no modification (useful first step to validate the
 * MITM hardware doesn't break the bus). YOU fill in the actual logic.
 *
 * WHERE TO GET THE HALDEX PROTOCOL DETAILS:
 *
 *  - If you forked OpenHaldex-C6 to your personal repo (recommended, FASL
 *    permits personal/non-commercial use): look at how their code identifies
 *    and modifies the demand frame. Translate into the call sites below.
 *
 *  - If you reverse-engineer yourself: sniff the bus with a 3rd CAN
 *    analyzer, identify the frame that changes with throttle/cornering
 *    inputs (that's the demand). Document the byte layout. Implement here.
 *
 *  - Do NOT copy any source code from OpenHaldex-C6 into this file —
 *    BOT32 is MIT-licensed and OpenHaldex is under FASL (incompatible).
 *    Implementing the same protocol in your own words is fine (facts are
 *    not copyrightable), but verbatim copy or "rewritten with minor
 *    changes" is not.
 */
#ifndef BOT32H_HALDEX_MITM_H
#define BOT32H_HALDEX_MITM_H

#include <Arduino.h>
#include "can_handler.h"

// Initialize the MITM module. Called once at boot from BOT32-Haldex.ino.
// In the default skeleton: does nothing (just logs). Add your init code
// here (e.g., MQB CRC table setup if you need it).
void haldex_mitm_init();

// =============================================================
//  Frame processing hooks — called from the main loop bridge
// =============================================================

// Called for every frame received on the PCM side, just before forwarding
// it to the Haldex side. You can MODIFY the frame in place (or leave it
// unchanged) based on the current mode (mode_state_get()).
//
// Typical use: when a non-stock mode is active, identify the pump-demand
// frame by its CAN ID, then rewrite the byte that encodes the demand
// value to force the desired pump %.
//
// Return true to forward the frame, false to DROP it (rarely useful).
bool haldex_mitm_process_pcm_frame(CanFrame& f);

// Called for every frame received on the Haldex side, just before
// forwarding to the PCM side. Use this to extract live state (current
// pump %, target %, etc.) for the ESP-NOW STATE broadcast.
//
// Typical use: parse the Haldex status frame, extract the current
// engagement value, store it via haldex_mitm_set_pump_pct(...).
//
// Return true to forward, false to drop (rarely useful).
bool haldex_mitm_process_haldex_frame(CanFrame& f);

// =============================================================
//  State getters/setters — used by espnow_peer to broadcast state
// =============================================================
// Called from haldex_mitm_process_*_frame() to update the cached values.
void haldex_mitm_set_pump_pct(uint8_t v);
void haldex_mitm_set_target_pct(uint8_t v);
void haldex_mitm_set_vehicle_kmh(uint8_t v);
void haldex_mitm_set_pedal_pct(uint8_t v);

// Called by espnow_peer.cpp when building the STATE packet.
uint8_t haldex_mitm_get_pump_pct();
uint8_t haldex_mitm_get_target_pct();
uint8_t haldex_mitm_get_vehicle_kmh();
uint8_t haldex_mitm_get_pedal_pct();

#endif // BOT32H_HALDEX_MITM_H
