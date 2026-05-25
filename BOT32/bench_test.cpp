/*
 * Bench test mode — implementation.
 */
#include "bench_test.h"
#include "config.h"
#include "settings.h"
#include "can_handler.h"
#include "coolant.h"
#include "vw_mqb.h"
#include "serial_proto.h"

// Per-frame rolling counter for MQB CRC (4-bit, 0..15)
static uint8_t  counter_wake     = 0;   // 0x3C0
static uint8_t  counter_engine   = 0;   // 0x641
static uint8_t  counter_airbag   = 0;   // 0x040
static uint8_t  counter_esp10    = 0;   // 0x116
static uint8_t  counter_tsk07    = 0;   // 0x31E
static uint8_t  counter_lh_eps   = 0;   // 0x32A

// Per-frame last TX timestamp (ms)
static uint32_t last_wake_ms      = 0;
static uint32_t last_engine_ms    = 0;
static uint32_t last_rpm_ms       = 0;
static uint32_t last_coolant_ms   = 0;
static uint32_t last_airbag_ms    = 0;
static uint32_t last_esp05_ms     = 0;
static uint32_t last_esp10_ms     = 0;
static uint32_t last_esp20_ms     = 0;
static uint32_t last_tsk07_ms     = 0;
static uint32_t last_lh_eps_ms    = 0;

void bench_test_init() {
  // nothing yet — state is local to this file
}

// Helper: send a frame on the configured bench bus + mirror to PC
static void bench_send(uint16_t id, const uint8_t* data, uint8_t len) {
  CanChannel ch = (settings_get().bench_test_bus == 1) ? CAN_OBD2 : CAN_CLUSTER;
  CanFrame f;
  f.id  = id;
  f.len = len;
  for (uint8_t i = 0; i < len; i++) f.data[i] = data[i];
  bool ok = can_send(ch, f);
  if (ok) serial_proto_report_tx(ch, f);
}

// Helper: build payload with MQB CRC + counter and send
static void bench_send_crc(uint16_t id, const uint8_t* template_data,
                            uint8_t len, uint8_t& counter) {
  uint8_t payload[8];
  for (uint8_t i = 0; i < len; i++) payload[i] = template_data[i];
  mqb_apply(id, payload, len, counter);
  counter = (counter + 1) & 0x0F;
  bench_send(id, payload, len);
}

bool bench_test_tick() {
  const Settings& s = settings_get();
  if (!s.bench_test_enabled) {
    return false;
  }
  if (!s.tx_enabled) {
    return true;  // bench wanted but TX globally off — signal preempt anyway
  }

  uint32_t now = millis();

  // ───── 0x3C0 Klemmen_Status_01 (wake) — 10 Hz, MQB CRC ─────
  // byte 2 = 0x03 (Kl.15 + Kl.S, ignition+start ON)
  if (now - last_wake_ms >= 100) {
    static const uint8_t WAKE_TEMPLATE[4] = { 0x00, 0x00, 0x03, 0x00 };
    bench_send_crc(0x3C0, WAKE_TEMPLATE, 4, counter_wake);
    last_wake_ms = now;
  }

  // ───── 0x641 Motor_Code_01 (engine alive) — 20 Hz, MQB CRC ─────
  if (now - last_engine_ms >= 50) {
    static const uint8_t ENGINE_TEMPLATE[8] = { 0x00, 0x10, 0x00, 0xE8, 0x03, 0x00, 0x00, 0x00 };
    bench_send_crc(0x641, ENGINE_TEMPLATE, 8, counter_engine);
    last_engine_ms = now;
  }

  // ───── 0x107 Motor_04 (RPM) — 20 Hz, NO CRC ─────
  // bytes 3-4 LE = rpm / 3  (r00li formula)
  if (now - last_rpm_ms >= 50) {
    uint16_t rpm_raw = s.bench_rpm / 3;
    uint8_t payload[8] = { 0, 0, 0,
                           (uint8_t)(rpm_raw & 0xFF),
                           (uint8_t)((rpm_raw >> 8) & 0xFF),
                           0, 0, 0 };
    bench_send(0x107, payload, 8);
    last_rpm_ms = now;
  }

  // ───── 0x647 Motor_09 (coolant from MAP) — 20 Hz, NO CRC ─────
  if (now - last_coolant_ms >= 50) {
    uint8_t byte0 = coolant_map_mbar_to_byte(
      (float)s.bench_map_mbar,
      s.map_min_mbar, s.map_max_mbar
    );
    uint8_t payload[8];
    coolant_build_motor_09(byte0, payload);
    bench_send(0x647, payload, 8);
    serial_proto_set_coolant_byte(byte0);
    last_coolant_ms = now;
  }

  // ───── 0x040 Airbag_01 — 10 Hz, MQB CRC (BENCH ONLY) ─────
  // Only if user explicitly disabled the airbag block.
  if (!s.block_airbag && (now - last_airbag_ms >= 100)) {
    static const uint8_t AIRBAG_TEMPLATE[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    bench_send_crc(0x040, AIRBAG_TEMPLATE, 8, counter_airbag);
    last_airbag_ms = now;
  }

  // ───── 0x106 ESP_05 — 10 Hz, NO CRC ─────
  if (now - last_esp05_ms >= 100) {
    static const uint8_t ESP05[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    bench_send(0x106, ESP05, 8);
    last_esp05_ms = now;
  }

  // ───── 0x116 ESP_10 — 10 Hz, MQB CRC ─────
  if (now - last_esp10_ms >= 100) {
    static const uint8_t ESP10_TEMPLATE[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    bench_send_crc(0x116, ESP10_TEMPLATE, 8, counter_esp10);
    last_esp10_ms = now;
  }

  // ───── 0x65D ESP_20 — 10 Hz, NO CRC ─────
  if (now - last_esp20_ms >= 100) {
    static const uint8_t ESP20[8] = { 0x00, 0x30, 0x2B, 0x12, 0x00, 0x00, 0xB4, 0x79 };
    bench_send(0x65D, ESP20, 8);
    last_esp20_ms = now;
  }

  // ───── 0x31E TSK_07 — 10 Hz, MQB CRC ─────
  if (now - last_tsk07_ms >= 100) {
    static const uint8_t TSK07_TEMPLATE[8] = { 0xCA, 0xEF, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x40 };
    bench_send_crc(0x31E, TSK07_TEMPLATE, 8, counter_tsk07);
    last_tsk07_ms = now;
  }

  // ───── 0x32A LH_EPS_01 — 10 Hz, MQB CRC ─────
  if (now - last_lh_eps_ms >= 100) {
    static const uint8_t LH_EPS_TEMPLATE[8] = { 0x4B, 0x08, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00 };
    bench_send_crc(0x32A, LH_EPS_TEMPLATE, 8, counter_lh_eps);
    last_lh_eps_ms = now;
  }

  return true;  // bench is active — caller should skip normal TX
}
