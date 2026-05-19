/*
 * Unified CAN handler — wraps 2x MCP2515 (shared SPI) behind a single API.
 *
 * Hardware: WaveShare 2-CH CAN HAT wired to ESP32 via Dupont jumpers.
 *   - 2x MCP2515 chips on the SAME SPI bus (SCK, MISO, MOSI shared)
 *   - 2x SIT65HVD230 3.3V CAN transceivers (one per controller)
 *   - Each MCP2515 has its own CS (chip select) and INT pins
 *
 * Channel naming:
 *   CAN_CLUSTER  = MCP2515 #0 (CS pin 5, INT pin 4)  -> MK7 cluster bus
 *   CAN_OBD2     = MCP2515 #1 (CS pin 25, INT pin 26) -> OBD-II port
 *
 * Both buses are 500 kbps standard 11-bit IDs.
 */
#ifndef BOT32_CAN_HANDLER_H
#define BOT32_CAN_HANDLER_H

#include <Arduino.h>

enum CanChannel {
  CAN_CLUSTER = 0,
  CAN_OBD2    = 1,
};

// Generic CAN message (works for both channels)
struct CanFrame {
  uint32_t id;          // 11-bit standard ID
  uint8_t  len;         // 0..8
  uint8_t  data[8];
  uint32_t timestamp;   // millis() at TX or RX
};

// Counters per channel — for diagnostics + USB live status
struct CanStats {
  uint32_t tx_ok;
  uint32_t tx_fail;
  uint32_t rx_count;
  uint32_t bus_errors;
  uint32_t last_tx_ms;
  uint32_t last_rx_ms;
};

// =============================================================
//  Lifecycle
// =============================================================

// Initialize both CAN channels. Call from setup().
// Returns true if both buses came up OK.
bool can_init();

// Periodic poll (call from loop()). Drains the RX queues of both buses
// and dispatches frames to registered listeners.
void can_poll();

// =============================================================
//  TX
// =============================================================

// Send a frame on the given channel. Returns true if queued OK.
// Hard-coded blocklist: FORBIDDEN_IDS (airbag) will NEVER transmit.
bool can_send(CanChannel ch, const CanFrame& frame);

// =============================================================
//  RX (callback model)
// =============================================================

// Callback signature: void onRx(CanChannel ch, const CanFrame& f)
typedef void (*CanRxCallback)(CanChannel ch, const CanFrame& f);

// Register a global RX callback for all frames on a given channel.
// Multiple listeners can be registered (up to MAX_LISTENERS).
void can_register_listener(CanChannel ch, CanRxCallback cb);

// =============================================================
//  Diagnostics
// =============================================================

CanStats can_get_stats(CanChannel ch);

// Recovery: try to bring a bus back if bus-off occurred.
bool can_recover(CanChannel ch);

#endif // BOT32_CAN_HANDLER_H
