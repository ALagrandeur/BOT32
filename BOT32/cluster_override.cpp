/*
 * cluster_override.cpp — implementation
 */
#include "cluster_override.h"
#include "can_handler.h"
#include "settings.h"
#include "obd2.h"
#include "vw_mqb.h"
#include "serial_proto.h"
#include "config.h"

// =============================================================
//  State
// =============================================================
static bool     g_trigger_pressed   = false;
static uint8_t  g_wba03_counter     = 0;     // our own rolling counter for the override TX
static uint32_t g_last_tx_ms        = 0;
static uint8_t  g_last_encoded_byte = 0;     // for diagnostics
static float    g_last_value_pct    = -1.0f; // for diagnostics

static const uint32_t OVERRIDE_TX_INTERVAL_MS = 25;  // 40 Hz to dominate gateway's 20 Hz

// =============================================================
//  Trigger detection — CAN listener
// =============================================================
static void on_cluster_rx_for_trigger(CanChannel ch, const CanFrame& f) {
  if (ch != CAN_CLUSTER) return;
  const Settings& s = settings_get();
  if (!s.cluster_override_enabled) return;
  if (f.id != s.display_trigger_can_id) return;
  if (s.display_trigger_byte_idx >= f.len) return;

  uint8_t v = f.data[s.display_trigger_byte_idx];
  if (v == s.display_trigger_pressed_value) {
    g_trigger_pressed = true;
  } else if (v == s.display_trigger_rest_value) {
    g_trigger_pressed = false;
  }
  // Any other value: keep current state (debounce against transient bits)
}

void cluster_override_init() {
  can_register_listener(CAN_CLUSTER, on_cluster_rx_for_trigger);
}

// =============================================================
//  Override TX — runs at 40 Hz while trigger pressed
// =============================================================
void cluster_override_tick(uint32_t now, bool safe_to_tx) {
  const Settings& s = settings_get();
  if (!s.cluster_override_enabled) return;
  if (!g_trigger_pressed) return;
  if (!safe_to_tx) return;
  if (now - g_last_tx_ms < OVERRIDE_TX_INTERVAL_MS) return;
  g_last_tx_ms = now;

  // Resolve the value to display based on configured source
  float val_pct = -1.0f;
  switch (s.display_value_source) {
    case 0: val_pct = obd2_get_last_ethanol_pct();         break;
    case 1: val_pct = obd2_get_last_haldex_blockage_pct(); break;
    default: return;  // unknown source
  }
  g_last_value_pct = val_pct;
  if (val_pct < 0.0f) return;  // no fresh value to display

  // Encode the % into byte[3] low nibble (capped 0..14, value resolution = 7%)
  //   0..7%   -> 0
  //   8..14%  -> 1
  //   ...
  //   98..100% -> 14
  // The MQB convention reserves 0x0F (15) for "no gear" so we cap at 14.
  uint8_t digit = (uint8_t)(val_pct / 7.0f);
  if (digit > 14) digit = 14;
  g_last_encoded_byte = digit;

  // Build the modified WBA_03 frame.
  // The cluster reads:
  //   byte[1] high nibble = lever character (0x10=P, 0x20=R, 0x30=N, 0x40=D, 0x50=S, 0x60=M)
  //   byte[1] low nibble  = rolling counter (set by mqb_apply)
  //   byte[3] low nibble  = engaged gear digit
  // We override byte[1] high nibble with the user-configured value (so the
  // letter on the cluster matches what the user wants — e.g. "D" to keep
  // a Drive-looking display, or experimental values to test).
  CanFrame f;
  f.id  = s.cluster_wba03_id;
  f.len = 8;
  f.data[0] = 0x00;                                  // CRC placeholder, filled by mqb_apply
  f.data[1] = s.display_override_byte1_high & 0xF0;  // counter goes in low nibble via mqb_apply
  f.data[2] = 0x00;
  f.data[3] = digit;                                 // value-to-display
  f.data[4] = 0x00;
  f.data[5] = 0x00;
  f.data[6] = 0x00;
  f.data[7] = 0x00;

  // Apply MQB counter + CRC (WBA_03 = 0x394 is in the MQB CRC table)
  mqb_apply(s.cluster_wba03_id, f.data, 8, g_wba03_counter);
  g_wba03_counter = (g_wba03_counter + 1) & 0x0F;

  bool ok = can_send(CAN_CLUSTER, f);
  if (ok) {
    serial_proto_report_tx(CAN_CLUSTER, f);
  }
}

// =============================================================
//  Diagnostic accessors
// =============================================================
bool    cluster_override_is_trigger_pressed()    { return g_trigger_pressed; }
uint8_t cluster_override_get_last_encoded_byte() { return g_last_encoded_byte; }
float   cluster_override_get_last_value_pct()    { return g_last_value_pct; }
