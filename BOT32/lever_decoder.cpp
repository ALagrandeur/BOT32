/*
 * Lever decoder — implementation.
 */
#include "lever_decoder.h"
#include "config.h"
#include "settings.h"

static char     current_lever = '?';
static uint8_t  current_gear  = 0;
static uint32_t last_wba03_ms = 0;

// Map byte 1 high nibble to lever character
static char decode_high_nibble(uint8_t nibble) {
  switch (nibble) {
    case 0x10: return 'P';
    case 0x20: return 'R';
    case 0x30: return 'N';
    case 0x40: return 'D';
    case 0x50: return 'S';
    case 0x60: return 'M';
    default:   return '?';
  }
}

static void on_cluster_rx(CanChannel ch, const CanFrame& f) {
  if (ch != CAN_CLUSTER) return;
  if (f.id != settings_get().cluster_wba03_id) return;  // user-configurable
  if (f.len < 4) return;

  char lever = decode_high_nibble(f.data[1] & 0xF0);
  if (lever == '?') return;  // garbled

  uint8_t gear = f.data[3] & 0x0F;
  if (gear < 1 || gear > 6) gear = 0;

  current_lever = lever;
  current_gear  = gear;
  last_wba03_ms = millis();
}

void lever_init() {
  can_register_listener(CAN_CLUSTER, on_cluster_rx);
}

char lever_get() {
  // Mark stale if no frame in 1 second (cluster should send WBA_03 at ~20 Hz)
  if (last_wba03_ms == 0) return '?';
  if (millis() - last_wba03_ms > 1000) return '?';
  return current_lever;
}

uint8_t lever_get_gear() {
  return current_gear;
}

bool lever_is_boost_mode() {
  char l = lever_get();
  // v1.6.0: N removed — only S/M trigger BOOST (P/R/N/D = silent)
  return (l == 'S' || l == 'M');
}

uint32_t lever_get_age_ms() {
  if (last_wba03_ms == 0) return UINT32_MAX;
  return millis() - last_wba03_ms;
}
