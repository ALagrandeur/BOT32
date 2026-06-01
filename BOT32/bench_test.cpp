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
#include "bench_frames.h"   // v3.4.0: live-toggleable "lamp killer" frame injector

// Per-frame rolling counter for MQB CRC (4-bit, 0..15)
static uint8_t  counter_wake     = 0;   // 0x3C0
static uint8_t  counter_engine   = 0;   // 0x641
static uint8_t  counter_airbag   = 0;   // 0x040
static uint8_t  counter_esp10    = 0;   // 0x116
static uint8_t  counter_tsk07    = 0;   // 0x31E
static uint8_t  counter_lh_eps   = 0;   // 0x32A
static uint8_t  counter_esp21    = 0;   // 0x0FD (v3.6.0 bench speedo)

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
static uint32_t last_esp21_ms     = 0;   // 0x0FD (v3.6.0 bench speedo)

void bench_test_init() {
  bench_frames_init();   // v3.4.0: all candidate lamp frames default OFF
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

  // ───── Speedometer — 20 Hz, MQB CRC — v3.6.2 ─────
  // PROVEN recipe from mk7-cluster-bench-controller (r00li): the needle needs
  // TWO frames sent IN PARALLEL, sharing one rolling counter:
  //   0x0FD ESP_21 : bytes 4-5 LE = vSpeed = round(km/h * 98.5)   (template 00 D0 1F 80 D8 0D 00 00)
  //   0x31B ESP_24 : bytes 2-3 LE = round(km/h * 1.35 * 100)      (template 00 00 00 00 00 01 00 00)
  // Without the parallel 0x31B the needle does NOT move (this was the v3.6.0 bug).
  if (now - last_esp21_ms >= 50) {
    uint16_t spd = (s.bench_speed_kmh > 260) ? 260 : s.bench_speed_kmh;
    uint16_t v_speed     = (uint16_t)((spd * 985UL + 5) / 10);          // km/h * 98.5
    uint16_t esp24_speed = (uint16_t)((spd * 135UL * 100UL + 50) / 100);// km/h * 1.35 * 100

    uint8_t esp21[8] = { 0x00, 0xD0, 0x1F, 0x80,
                         (uint8_t)(v_speed & 0xFF),
                         (uint8_t)((v_speed >> 8) & 0xFF),
                         0x00, 0x00 };
    // ESP_24 template uses THIS car's real captured steady-state (the same
    // 0x31B payload that cleared TC/ABS/brake on the bench) with bytes 2-3
    // overwritten by the kombi speed. So 0x31B drives the needle AND keeps
    // those 3 lamps off. byte0=CRC + byte1 low-nibble=counter (mqb_apply).
    uint8_t esp24[8] = { 0x67, 0x06,
                         (uint8_t)(esp24_speed & 0xFF),
                         (uint8_t)((esp24_speed >> 8) & 0xFF),
                         0x00, 0x00, 0x00, 0x7E };

    // Both frames share the same counter value this cycle (mqb_apply writes the
    // counter into byte1 low nibble + the CRC into byte0 for each ID).
    uint8_t shared = counter_esp21;
    bench_send_crc(0x0FD, esp21, 8, shared);
    shared = counter_esp21;                 // reuse same counter for the partner frame
    bench_send_crc(0x31B, esp24, 8, shared);
    counter_esp21 = (counter_esp21 + 1) & 0x0F;
    last_esp21_ms = now;
  }

  // ───── v3.4.0: user-toggled "lamp killer" candidate frames ─────
  // Each enabled frame is replayed at its own period with CRC regen where
  // needed. All default OFF; the user enables them from the UI to discover
  // which frame clears which warning lamp.
  bench_frames_tick(now);

  return true;  // bench is active — caller should skip normal TX
}
