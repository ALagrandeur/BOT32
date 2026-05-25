/*
 * haldex_link.h — BOT32 client for a Haldex AWD MITM module
 *
 * BOT32 dialogues with a SEPARATE MITM device (running on a 2nd ESP32 with
 * 2 CAN modules, or on dedicated OpenHaldex-C6 hardware) over the chassis
 * CAN bus. The MITM device handles the actual Haldex bus man-in-the-middle
 * and broadcasts its state. BOT32 is the CLIENT — it reads the broadcasts
 * and sends mode commands.
 *
 * Protocol (publicly documented facts):
 *   - State broadcast on a configurable CAN ID (default 0x6B0), 8 bytes,
 *     containing: current Haldex pump engagement %, lock target %, vehicle
 *     speed, mode flags, current mode number, throttle pedal %.
 *   - Mode command on a configurable CAN ID (default 0x6B1), 8 bytes,
 *     with the requested mode number in byte 0.
 *
 * Mode numbers (matches the publicly documented convention):
 *   0 = Stock     — pass-through, normal OEM Haldex behavior
 *   1 = FWD       — force front-wheel-drive (pump 0%) — race burnout mode
 *   2 = 5050      — force 50/50 split (pump 100%) — race launch mode
 *   3 = 6040
 *   4 = 7525
 *   5 = Expert    — user-defined behavior in the MITM module
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IMPORTANT — Attribution / origin of the protocol facts:
 *
 * The CAN-based broadcast/command protocol used here was originally designed
 * and publicly documented by Forbes Automotive for their OpenHaldex-C6
 * project (https://github.com/Forbes-Automotive/OpenHaldex-C6).
 *
 * BOT32 does NOT include any source code, PCB designs, or compiled binaries
 * from OpenHaldex-C6. This file contains only freshly-written code that
 * speaks to that publicly documented protocol. OpenHaldex-C6 is distributed
 * under the Forbes Automotive Source-Available License (FASL v1.0); users
 * who want a working Haldex MITM device should either purchase the official
 * hardware or run OpenHaldex-C6 firmware on their own ESP32 (permitted by
 * FASL for personal/non-commercial use).
 *
 * Big thanks to Forbes Automotive for their open reverse-engineering work
 * on the VW MQB Haldex platform.
 * ─────────────────────────────────────────────────────────────────────────
 */
#ifndef BOT32_HALDEX_LINK_H
#define BOT32_HALDEX_LINK_H

#include <Arduino.h>
#include "can_handler.h"

// Operating modes (numbering matches the publicly documented convention)
enum HaldexMode {
  HALDEX_MODE_STOCK  = 0,
  HALDEX_MODE_FWD    = 1,
  HALDEX_MODE_5050   = 2,
  HALDEX_MODE_6040   = 3,
  HALDEX_MODE_7525   = 4,
  HALDEX_MODE_EXPERT = 5,
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
  uint8_t   current_mode;         // 0..5 per HaldexMode enum
  uint8_t   pedal_pct;            // throttle pedal position, 0..100
};

// Initialize the link. Call AFTER can_init() in setup().
// Registers CAN listeners; activation depends on settings.haldex_enabled.
void haldex_link_init();

// Send a "set mode" command to the MITM module.
// Returns true if the frame was queued successfully (does not imply ACK by
// the MITM module). Refuses to send if mode > 5 or haldex_enabled is false.
bool haldex_link_set_mode(uint8_t mode);

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
