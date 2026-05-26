/*
 * cluster_override.h — Configurable cluster display override (v2.2)
 *
 * When a user-defined trigger CAN signal is observed in its "pressed" state,
 * BOT32 starts transmitting a MODIFIED WBA_03 (0x394) frame at 40 Hz on the
 * cluster bus to dominate the gateway's broadcast (which runs at ~20 Hz).
 * The cluster then displays the override value instead of the real lever.
 *
 * Currently supported display value sources:
 *   0 = Ethanol content %  (from obd2.cpp UDS poll, DID 0xF452)
 *   1 = Haldex blockage %  (from obd2.cpp UDS poll, DID 0x2BF3)
 *
 * Encoding into WBA_03:
 *   - byte[1] high nibble = user-configurable (e.g. 0x40=D, 0x50=S, or
 *     experimental values to see what the cluster displays)
 *   - byte[3] = value-to-display encoded as a digit (0..14 via val_pct / 7)
 *
 * Trigger detection:
 *   The trigger is configured by:
 *     - CAN ID to watch (e.g. 0x0FD for TC button, or any other ID)
 *     - Byte index within the frame (0..7)
 *     - Value at rest (e.g. 0x00)
 *     - Value when pressed (e.g. 0x03)
 *
 * Default trigger = TC button (0x0FD byte 6, 0x00/0x03) — confirmed in
 * vehicle capture 2026-05-26. The user can pick any other identified
 * button by editing the settings in the UI.
 *
 * SAFETY: this module ONLY transmits when:
 *   - cluster_override_enabled == true (master toggle)
 *   - trigger is currently in "pressed" state
 *   - tx_enabled is true
 *   - currentMode is NOT BOOT or SAFE_FAULT (handled by caller in BOT32.ino)
 */
#ifndef BOT32_CLUSTER_OVERRIDE_H
#define BOT32_CLUSTER_OVERRIDE_H

#include <Arduino.h>

// Initialize the module (registers a CAN listener on CAN_CLUSTER for trigger
// detection). Call once at boot AFTER can_init().
void cluster_override_init();

// Tick — call from loop(). Transmits the override WBA_03 at 40 Hz when
// trigger is pressed (and the feature is enabled).
// Pass `safe_to_tx = true` only when the main state machine allows TX
// (currentMode != BOOT && != SAFE_FAULT && settings.tx_enabled).
void cluster_override_tick(uint32_t now, bool safe_to_tx);

// Diagnostic — current trigger state for UI display
bool cluster_override_is_trigger_pressed();

// Diagnostic — last value sent in byte[3] (for UI feedback)
uint8_t cluster_override_get_last_encoded_byte();

// Diagnostic — last raw value % used to compute the encoded byte
float cluster_override_get_last_value_pct();

#endif // BOT32_CLUSTER_OVERRIDE_H
