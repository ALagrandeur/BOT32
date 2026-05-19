/*
 * Coolant mapping — implementation.
 */
#include "coolant.h"
#include "config.h"
#include "vw_mqb.h"

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
  float scale,
  float offset_c
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

  // 2. Compute target temp with DEAD ZONE SKIP
  //    Useful range = [50, safe_low] + [safe_high, 130]
  //    where safe_low = 80 - 1, safe_high = 110 + 1 (safe margin avoids
  //    round-trip rounding landing back in dead zone)
  float safe_low  = CLUSTER_DEAD_ZONE_LOW_C  - DEAD_ZONE_SAFE_MARGIN_C;
  float safe_high = CLUSTER_DEAD_ZONE_HIGH_C + DEAD_ZONE_SAFE_MARGIN_C;
  float len_bottom = safe_low  - COOLANT_TEMP_MIN_C;   // 50 .. 79  = 29
  float len_top    = COOLANT_TEMP_MAX_C - safe_high;   // 111 .. 130 = 19
  float total      = len_bottom + len_top;             // 48

  float temp_c;
  if (total <= 0.0f) {
    temp_c = (COOLANT_TEMP_MIN_C + COOLANT_TEMP_MAX_C) * 0.5f;
  } else {
    float useful_pos = ratio * total;
    if (useful_pos <= len_bottom) {
      temp_c = COOLANT_TEMP_MIN_C + useful_pos;          // 50 .. 79
    } else {
      temp_c = safe_high + (useful_pos - len_bottom);    // 111 .. 130
    }
  }

  // 3. Apply scale + offset, then convert to byte
  temp_c = temp_c * scale + offset_c;
  return coolant_temp_c_to_byte(temp_c);
}

void coolant_build_motor_09(uint8_t coolant_byte, uint8_t* out) {
  out[0] = coolant_byte;
  for (int i = 0; i < 7; i++) {
    out[1 + i] = MOTOR_09_TAIL[i];
  }
}
