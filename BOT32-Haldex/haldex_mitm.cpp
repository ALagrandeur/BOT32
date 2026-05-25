/*
 * haldex_mitm.cpp — STUB IMPLEMENTATION
 *
 * Default behavior: pure pass-through (no frame modification, no state
 * extraction). The hardware works, the ESP-NOW link works, but the cluster
 * sees what the OEM PCM is asking for (no override yet).
 *
 * YOU fill in the marked TODO blocks with the actual Haldex bus protocol
 * details from your own knowledge (fork of OpenHaldex-C6, vehicle sniff,
 * or other reverse-engineering source). See haldex_mitm.h for the legal
 * boundary (do not copy code from OpenHaldex into this file; reimplement
 * using your understanding of the protocol).
 *
 * USE THIS SKELETON FOR INITIAL TESTING:
 *   1. Flash BOT32-Haldex.ino as-is
 *   2. Wire MITM into the Haldex bus (PCM side + Haldex side terminal blocks)
 *   3. Drive the car normally
 *   4. Verify: no AWD fault codes, normal AWD behavior
 *   5. Verify: BOT32 main UI shows MITM as "alive" (ESP-NOW STATE arriving)
 *   6. THEN add the actual modification logic below.
 */
#include "haldex_mitm.h"
#include "mode_state.h"

// =============================================================
//  Cached live state — populated by haldex_mitm_process_haldex_frame()
//  Reported via ESP-NOW to BOT32 main.
// =============================================================
static uint8_t g_pump_pct   = 0;
static uint8_t g_target_pct = 0;
static uint8_t g_vehicle_kmh = 0;
static uint8_t g_pedal_pct  = 0;

void haldex_mitm_init() {
  Serial.println("[haldex_mitm] STUB init — pass-through mode");
  Serial.println("[haldex_mitm] Fill in the TODO blocks in haldex_mitm.cpp");
  Serial.println("[haldex_mitm] before this can actually modify pump demand.");
}

// =============================================================
//  PCM -> Haldex frame processing
// =============================================================
bool haldex_mitm_process_pcm_frame(CanFrame& f) {
  uint8_t mode = mode_state_get();

  // ─────────────────────────────────────────────────────────────────
  // TODO #1 — IDENTIFY + MODIFY THE PUMP DEMAND FRAME
  //
  // Pattern (you fill in the specifics):
  //
  //   if (f.id == YOUR_HALDEX_DEMAND_CAN_ID) {
  //     // Find the byte that encodes the demand (e.g., 0..255 => 0..100%)
  //     switch (mode) {
  //       case 1: f.data[N] = 0;   break;  // FWD     -> pump 0%
  //       case 2: f.data[N] = 255; break;  // 5050    -> pump 100%
  //       case 3: f.data[N] = 153; break;  // 6040    -> pump ~60%
  //       case 4: f.data[N] = 64;  break;  // 7525    -> pump ~25%
  //       // case 0 (STOCK) and 5 (EXPERT) leave unchanged
  //     }
  //     // If the frame uses MQB CRC + counter, recompute them here too
  //     // (your knowledge of the MQB constants table for this CAN ID).
  //   }
  //
  // For now, pass-through unchanged.
  // ─────────────────────────────────────────────────────────────────

  return true;  // forward frame to Haldex side
}

// =============================================================
//  Haldex -> PCM frame processing (state extraction)
// =============================================================
bool haldex_mitm_process_haldex_frame(CanFrame& f) {

  // ─────────────────────────────────────────────────────────────────
  // TODO #2 — EXTRACT LIVE STATE FROM HALDEX ECU BROADCASTS
  //
  // Pattern:
  //
  //   if (f.id == YOUR_HALDEX_STATUS_CAN_ID) {
  //     g_pump_pct   = scale_byte_to_pct(f.data[N1]);
  //     g_target_pct = scale_byte_to_pct(f.data[N2]);
  //     // ... etc
  //   }
  //
  //   Vehicle speed and pedal % might come from OTHER frames on this
  //   bus (or from the PCM side). Look in your sources.
  //
  // For now, the live state stays at 0 — BOT32 UI will show 0% etc.
  // ─────────────────────────────────────────────────────────────────

  return true;  // forward frame to PCM side
}

// =============================================================
//  Cached state accessors
// =============================================================
void haldex_mitm_set_pump_pct(uint8_t v)    { g_pump_pct = v; }
void haldex_mitm_set_target_pct(uint8_t v)  { g_target_pct = v; }
void haldex_mitm_set_vehicle_kmh(uint8_t v) { g_vehicle_kmh = v; }
void haldex_mitm_set_pedal_pct(uint8_t v)   { g_pedal_pct = v; }

uint8_t haldex_mitm_get_pump_pct()    { return g_pump_pct; }
uint8_t haldex_mitm_get_target_pct()  { return g_target_pct; }
uint8_t haldex_mitm_get_vehicle_kmh() { return g_vehicle_kmh; }
uint8_t haldex_mitm_get_pedal_pct()   { return g_pedal_pct; }
