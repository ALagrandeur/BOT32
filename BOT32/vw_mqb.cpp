/*
 * VW MQB helpers — implementation.
 * Port of mk7-cluster-bench-controller/webui/vw_mqb.py
 */
#include "vw_mqb.h"
#include "config.h"

// Motor_09 tail bytes (declared extern in config.h)
const uint8_t MOTOR_09_TAIL[7] = { 0xFD, 0xFF, 0x7F, 0x00, 0x00, 0x00, 0xC1 };

// CRC table (built at runtime in vw_mqb_init)
uint8_t MQB_CRC8H2F[256];

// =============================================================
//  Per-CAN-ID constants (16 entries each = one per counter 0..15)
//  Source: openpilot/opendbc/car/volkswagen/mqbcan.py
//  Only IDs we MIGHT use are included.
// =============================================================

// Klemmen_Status_01 (wake) — all 0xC3
static const uint8_t MQB_CONST_0x3C0[16] = {
  0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3,
  0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3,
};

// Motor_Code_01 — all 0x47
static const uint8_t MQB_CONST_0x641[16] = {
  0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47,
  0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47,
};

// WBA_03 (gear position)
static const uint8_t MQB_CONST_0x394[16] = {
  0x47, 0x94, 0x92, 0x6A, 0x67, 0xB5, 0x0D, 0x38,
  0xE3, 0x8A, 0x5D, 0xB4, 0x54, 0xAB, 0xAE, 0x27,
};

// TSK_07
static const uint8_t MQB_CONST_0x31E[16] = {
  0x78, 0x68, 0x3A, 0x31, 0x16, 0x08, 0x4F, 0xDE,
  0xF7, 0x35, 0x19, 0xE6, 0x28, 0x2F, 0x59, 0x82,
};

// ESP_10 — all 0xAC
static const uint8_t MQB_CONST_0x116[16] = {
  0xAC, 0xAC, 0xAC, 0xAC, 0xAC, 0xAC, 0xAC, 0xAC,
  0xAC, 0xAC, 0xAC, 0xAC, 0xAC, 0xAC, 0xAC, 0xAC,
};

// LH_EPS_01 — all 0x29
static const uint8_t MQB_CONST_0x32A[16] = {
  0x29, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29,
  0x29, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29,
};

// Airbag_01 — all 0x40 (NEVER TX from us, but included for diag tools)
static const uint8_t MQB_CONST_0x040[16] = {
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
};

const uint8_t* mqb_const_for_addr(uint16_t addr) {
  switch (addr) {
    case 0x040: return MQB_CONST_0x040;
    case 0x116: return MQB_CONST_0x116;
    case 0x31E: return MQB_CONST_0x31E;
    case 0x32A: return MQB_CONST_0x32A;
    case 0x394: return MQB_CONST_0x394;
    case 0x3C0: return MQB_CONST_0x3C0;
    case 0x641: return MQB_CONST_0x641;
    default:    return nullptr;
  }
}

bool mqb_has_checksum(uint16_t addr) {
  return mqb_const_for_addr(addr) != nullptr;
}

// =============================================================
//  Build CRC8H2F table (polynomial 0x2F, no init, no XOR)
// =============================================================
void vw_mqb_init() {
  for (int i = 0; i < 256; i++) {
    uint8_t c = (uint8_t)i;
    for (int j = 0; j < 8; j++) {
      if (c & 0x80) {
        c = (uint8_t)(((c << 1) ^ 0x2F) & 0xFF);
      } else {
        c = (uint8_t)((c << 1) & 0xFF);
      }
    }
    MQB_CRC8H2F[i] = c;
  }
}

// =============================================================
//  Compute MQB checksum
//  Mirror of openpilot's volkswagen_mqb_meb_checksum
// =============================================================
uint8_t mqb_checksum(uint16_t addr, const uint8_t* data, uint8_t len) {
  if (len < 2) return 0;
  uint8_t crc = 0xFF;
  for (uint8_t i = 1; i < len; i++) {
    crc ^= data[i];
    crc = MQB_CRC8H2F[crc];
  }
  const uint8_t* k = mqb_const_for_addr(addr);
  if (k != nullptr) {
    uint8_t counter = data[1] & 0x0F;
    crc ^= k[counter];
    crc = MQB_CRC8H2F[crc];
  }
  return crc ^ 0xFF;
}

// =============================================================
//  Apply counter + CRC to payload in-place
// =============================================================
void mqb_apply(uint16_t addr, uint8_t* payload, uint8_t len, uint8_t counter) {
  if (len < 2) return;
  payload[1] = (payload[1] & 0xF0) | (counter & 0x0F);
  payload[0] = 0;
  payload[0] = mqb_checksum(addr, payload, len);
}
