/*
 * haldex_link.h — BOT32 main-side CLIENT for the Haldex AWD MITM module (X2)
 *
 * BOT32 main dialogues with a SEPARATE MITM device — the ESP32-CAN-X2 running
 * the BOT32-HALDEX firmware (private repo) — installed in series on the Haldex
 * bus. BOT32 main is the CLIENT: it reads the MITM's STATE and sends mode +
 * passthrough commands.
 *
 * Transport: ESP-NOW ONLY (v3.2.0; the legacy CAN transport was removed).
 * Wire format lives in haldex_espnow.cpp / docs/haldex_integration.md:
 *   magic 0xBA 0xB0; 0x01 STATE (10B), 0x02 SET_MODE (4B), 0x03 SET_PASSTHROUGH (4B).
 *
 * Mode numbers (3 supported — STOCK/FWD/5050; 6040/7525/Expert removed v3.2.0):
 *   0 = Stock     — pass-through, normal OEM Haldex behavior
 *   1 = FWD       — force front-wheel-drive (pump 0%) — race burnout mode
 *   2 = 5050      — force 50/50 split (pump 100%) — race launch mode
 *
 * ─────────────────────────────────────────────────────────────────────────
 * Attribution: the MQB Haldex protocol knowledge was openly reverse-engineered
 * by the OpenHaldex community (Forbes Automotive — OpenHaldex-C6, FASL v1.0).
 * BOT32 includes NO OpenHaldex source — only freshly-written code using the
 * public protocol facts. Thanks to Forbes Automotive for the open RE work.
 * ─────────────────────────────────────────────────────────────────────────
 */
#ifndef BOT32_HALDEX_LINK_H
#define BOT32_HALDEX_LINK_H

#include <Arduino.h>
#include "can_handler.h"

// Operating modes (v3.2.0: 3 supported modes — 6040/7525/Expert removed)
enum HaldexMode {
  HALDEX_MODE_STOCK  = 0,
  HALDEX_MODE_FWD    = 1,
  HALDEX_MODE_5050   = 2,
};

// Live state snapshot of the Haldex MITM module
struct HaldexState {
  bool      valid;          // true once at least one broadcast has been received
  uint32_t  last_rx_ms;     // millis() when last frame arrived
  uint8_t   raw[8];         // raw payload of last broadcast, for inspection
  uint8_t   len;
  // Parsed fields — interpreted from the public protocol documentation
  uint8_t   pump_engagement_pct;  // current Haldex pump duty, 0..100
  uint8_t   lock_target_pct;      // commanded lock target, 0..100
  uint8_t   vehicle_kmh;          // vehicle speed (low-res, 1 byte)
  uint8_t   current_mode;         // 0..2 per HaldexMode enum
  uint8_t   pedal_pct;            // throttle pedal position, 0..100
  uint8_t   passthrough;          // v3.2.0: 1 = X2 transparent (safe), 0 = MITM armed
};

// Initialize the link. Call AFTER can_init() in setup().
// Registers CAN listeners; activation depends on settings.haldex_enabled.
void haldex_link_init();

// Send a "set mode" command to the MITM module (ESP-NOW).
// Returns true if the packet was queued (does not imply ACK). Refuses to send
// if mode > 2 or haldex_enabled is false.
bool haldex_link_set_mode(uint8_t mode);

// v3.2.0: send a "set passthrough" command to the MITM (ESP-NOW).
// passthrough=true  -> X2 is a transparent bridge (safe, nothing modified).
// passthrough=false -> X2 arms the MITM (FWD/5050 will modify frames).
// Refuses if haldex_enabled is false.
bool haldex_link_set_passthrough(bool passthrough);

// Get a snapshot of the last received state.
HaldexState haldex_link_get_state();

// Age (ms) since the last broadcast was received. UINT32_MAX if never.
uint32_t haldex_link_get_age_ms();

// Human-readable name for a mode number (for UI / serial logs).
const char* haldex_mode_name(uint8_t mode);

// Push a new state from an alternative transport (e.g., the ESP-NOW
// handler in haldex_espnow.cpp). Used internally to unify state delivery
// regardless of which transport delivered the frame.
void haldex_link_update_state(const HaldexState& new_state);

#endif // BOT32_HALDEX_LINK_H
