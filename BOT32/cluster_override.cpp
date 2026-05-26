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

  // v2.2.1: in bench mode with force_override on, bypass trigger detection
  // (so the cluster shows the value without simulating the trigger frame).
  bool trigger_active = g_trigger_pressed;
  if (s.bench_test_enabled && s.bench_force_override) {
    trigger_active = true;
  }
  if (!trigger_active) return;
  if (!safe_to_tx) return;
  if (now - g_last_tx_ms < OVERRIDE_TX_INTERVAL_MS) return;
  g_last_tx_ms = now;

  // Resolve the value to display.
  // - Real vehicle: read live from OBD2 cache.
  // - Bench mode: use the slider (bench_display_value_pct) since OBD2 won't
  //   respond on a standalone cluster bench.
  float val_pct = -1.0f;
  if (s.bench_test_enabled) {
    val_pct = (float)s.bench_display_value_pct;
  } else {
    switch (s.display_value_source) {
      case 0: val_pct = obd2_get_last_ethanol_pct();         break;
      case 1: val_pct = obd2_get_last_haldex_blockage_pct(); break;
      default: return;
    }
  }
  g_last_value_pct = val_pct;
  if (val_pct < 0.0f) return;

  // v2.3.0: encode byte[3] based on configurable mode so the user can
  // experiment with how the cluster displays the % value.
  uint8_t v = (uint8_t)val_pct;  // cap to 0-255 implicitly; ethanol is 0-100
  if (v > 100) v = 100;
  uint8_t encoded;
  switch (s.display_byte3_value_mode) {
    case 0: default:
      // RAW: pass the full value byte directly (e.g. 11% -> 0x0B in byte[3])
      // Cluster might show the full number, partial nibble, or special code.
      encoded = v;
      break;
    case 1:
      // LEGACY ÷7: value / 7 capped at 14 (v2.2 behavior, low nibble only)
      encoded = (v / 7);
      if (encoded > 14) encoded = 14;
      break;
    case 2:
      // TENS DIGIT: v / 10 in low nibble (0..10), shows the "tens" of the %
      encoded = (v / 10) & 0x0F;
      break;
    case 3:
      // UNITS DIGIT: v % 10 in low nibble (0..9), shows the "units" of the %
      encoded = (v % 10) & 0x0F;
      break;
  }
  g_last_encoded_byte = encoded;

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
  f.data[3] = encoded;                               // value-to-display (encoded per mode)
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
