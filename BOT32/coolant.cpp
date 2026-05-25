/*
 * Coolant mapping + real-coolant sniffer — implementation.
 */
#include "coolant.h"
#include "config.h"
#include "vw_mqb.h"
#include "settings.h"

float coolant_byte_to_temp_c(uint8_t b) {
  return (float)b * COOLANT_FORMULA_SCALE + COOLANT_FORMULA_OFFSET;
}

uint8_t coolant_temp_c_to_byte(float temp_c) {
  float raw = (temp_c - COOLANT_FORMULA_OFFSET) / COOLANT_FORMULA_SCALE;
  if (raw < 0.0f)   raw = 0.0f;
  if (raw > 255.0f) raw = 255.0f;
  return (uint8_t)(raw + 0.5f);  // round
}

uint8_t coolant_map_mbar_to_byte(
  float map_mbar,
  float map_min_mbar,
  float map_max_mbar,
  bool  use_dead_zone
) {
  // 1. Clamp MAP, compute normalized ratio [0, 1]
  float ratio;
  if (map_max_mbar == map_min_mbar) {
    ratio = 0.5f;
  } else {
    ratio = (map_mbar - map_min_mbar) / (map_max_mbar - map_min_mbar);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
  }

  float temp_c;

  if (use_dead_zone) {
    // 50/50 visual split that skips the cluster dead zone [80, 110] C.
    //   ratio = 0.00  ->  50C   (cold needle)
    //   ratio = 0.50- ->  79C   (just before dead zone)
    //                    JUMP
    //   ratio = 0.50+ -> 111C   (just after dead zone)
    //   ratio = 1.00  -> 130C   (red zone)
    float safe_low  = CLUSTER_DEAD_ZONE_LOW_C  - DEAD_ZONE_SAFE_MARGIN_C;  // 79
    float safe_high = CLUSTER_DEAD_ZONE_HIGH_C + DEAD_ZONE_SAFE_MARGIN_C;  // 111

    if (ratio < 0.5f) {
      float bottom_range = safe_low - COOLANT_TEMP_MIN_C;  // 79 - 50 = 29
      temp_c = COOLANT_TEMP_MIN_C + (ratio * 2.0f) * bottom_range;
    } else {
      float top_range = COOLANT_TEMP_MAX_C - safe_high;    // 130 - 111 = 19
      temp_c = safe_high + ((ratio - 0.5f) * 2.0f) * top_range;
    }
  } else {
    // LINEAR mapping (v1.6.0 default — bench tests show cluster is linear)
    //   ratio = 0.00  ->  50C   (cold needle)
    //   ratio = 0.50  ->  90C   (middle of gauge, smooth)
    //   ratio = 1.00  -> 130C   (red zone)
    temp_c = COOLANT_TEMP_MIN_C + ratio * (COOLANT_TEMP_MAX_C - COOLANT_TEMP_MIN_C);
  }

  return coolant_temp_c_to_byte(temp_c);
}

void coolant_build_motor_09(uint8_t coolant_byte, uint8_t* out) {
  out[0] = coolant_byte;
  for (int i = 0; i < 7; i++) {
    out[1 + i] = MOTOR_09_TAIL[i];
  }
}

// =============================================================
//  Real coolant SNIFFER
// =============================================================

static uint8_t  last_real_byte = 0;
static uint32_t last_real_ms   = 0;

static void on_cluster_motor09(CanChannel ch, const CanFrame& f) {
  if (ch != CAN_CLUSTER) return;
  if (f.id != settings_get().cluster_motor09_id) return;
  if (f.len < 1) return;
  last_real_byte = f.data[0];
  last_real_ms   = millis();
}

void coolant_sniffer_init() {
  can_register_listener(CAN_CLUSTER, on_cluster_motor09);
}

float coolant_get_real_temp_c() {
  if (last_real_ms == 0) return -1.0f;  // no data yet
  return coolant_byte_to_temp_c(last_real_byte);
}

uint8_t coolant_get_real_byte() {
  return last_real_byte;
}

uint32_t coolant_get_real_age_ms() {
  if (last_real_ms == 0) return UINT32_MAX;
  return millis() - last_real_ms;
}
