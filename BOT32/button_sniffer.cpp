/*
 * Button / state sniffer — implementation (v2.8.0).
 */
#include "button_sniffer.h"
#include "config.h"
#include "can_handler.h"

// === Hand brake (KOMBI_01 0x30B, byte[2] bit 7) ===
static bool     hb_active   = false;
static uint32_t hb_ms       = 0;

// === MFSW OK button (0x5BF, byte[0]) ===
//   raw value 0x07 = released, 0x00 = pressed
static bool     ok_pressed  = false;
static uint32_t ok_ms       = 0;

static void on_cluster_rx(CanChannel ch, const CanFrame& f) {
  if (ch != CAN_CLUSTER) return;

  if (f.id == CAN_ID_KOMBI_01 && f.len >= 3) {
    bool now_active = (f.data[2] & 0x80) != 0;
    hb_active = now_active;
    hb_ms     = millis();
    return;
  }

  if (f.id == CAN_ID_MFSW && f.len >= 1) {
    // 0x00 = pressed; any non-zero (typically 0x07) = released.
    // Be lenient — only the "pressed" state is 0x00.
    ok_pressed = (f.data[0] == 0x00);
    ok_ms      = millis();
    return;
  }
}

void button_sniffer_init() {
  can_register_listener(CAN_CLUSTER, on_cluster_rx);
}

bool button_sniffer_handbrake_active()    { return hb_active; }
uint32_t button_sniffer_handbrake_age_ms() {
  if (hb_ms == 0) return UINT32_MAX;
  return millis() - hb_ms;
}

bool button_sniffer_ok_pressed()         { return ok_pressed; }
uint32_t button_sniffer_ok_age_ms() {
  if (ok_ms == 0) return UINT32_MAX;
  return millis() - ok_ms;
}
