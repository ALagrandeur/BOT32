/*
 * Version: v2.3.3 — https://github.com/ALagrandeur/BOT32/releases/tag/v2.3.3
 * BOT32 — In-vehicle boost-on-coolant override for VW MK7 cluster.
 *
 * Architecture (Hardware: WaveShare 2-CH CAN HAT wired to ESP32 via Dupont):
 *   - CAN0 (MCP2515 #0 on shared SPI, CS=5, INT=4) → MK7 cluster bus
 *       └── TX Motor_09 (0x647) coolant override
 *       └── RX WBA_03 (0x394) gear lever
 *   - CAN1 (MCP2515 #1 on shared SPI, CS=25, INT=26) → OBD-II port
 *       └── TX UDS query DID 0x39C0 (MAP) @ 5 Hz
 *       └── RX UDS response from engine ECU
 *   - USB serial @ 115200 → debug/config interface to PC web UI
 *   - NVS storage → persistent user settings
 *
 *   Both MCP2515 use the SAME SPI bus (SCK 18, MISO 19, MOSI 23).
 *   Both run at 3.3V via SIT65HVD230 transceivers — NO level shifter needed.
 *
 * State machine:
 *   BOOT       (5s listen-only at startup)
 *   SILENT     (lever P/R/D: no TX, cluster shows real coolant)
 *   BOOST      (lever S/M: poll MAP via OBD2, TX Motor_09 override)
 *   SAFE_FAULT (TX disabled by user or fatal error: no TX)
 *
 * Libraries required (install via Arduino IDE Library Manager):
 *   - ACAN2515       (Pierre Molinaro)  — drives both MCP2515 chips
 *   - ArduinoJson    (Benoit Blanchon)  — USB serial protocol
 */
#include "config.h"
#include "vw_mqb.h"
#include "coolant.h"
#include "can_handler.h"
#include "obd2.h"
#include "lever_decoder.h"
#include "settings.h"
#include "serial_proto.h"
#include "bench_test.h"
#include "haldex_link.h"
#include "cluster_override.h"

// =============================================================
//  State
// =============================================================
enum Mode {
  MODE_BOOT,
  MODE_SILENT,
  MODE_BOOST,
  MODE_SAFE_FAULT,
  MODE_BENCH_TEST,
};

static Mode     currentMode    = MODE_BOOT;
static uint32_t bootStartMs    = 0;
static uint32_t lastTxMs       = 0;
static uint8_t  lastCoolantByte = 0x80;

static const char* mode_name(Mode m) {
  switch (m) {
    case MODE_BOOT:       return "BOOT";
    case MODE_SILENT:     return "SILENT";
    case MODE_BOOST:      return "BOOST";
    case MODE_SAFE_FAULT: return "SAFE_FAULT";
    case MODE_BENCH_TEST: return "BENCH_TEST";
    default:              return "?";
  }
}

// =============================================================
//  Setup
// =============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("================================");
  Serial.println("  BOT32 - boost gauge override");
  Serial.println("================================");

  pinMode(PIN_LED_STATUS, OUTPUT);
  digitalWrite(PIN_LED_STATUS, HIGH);  // ON at boot

  // Initialize subsystems
  vw_mqb_init();
  settings_init();
  if (!can_init()) {
    Serial.println("[FATAL] CAN init failed — entering SAFE_FAULT");
    currentMode = MODE_SAFE_FAULT;
  } else {
    obd2_init();
    lever_init();
    coolant_sniffer_init();   // sniff real Motor_09 from cluster bus
    bench_test_init();
    haldex_link_init();       // client for external Haldex MITM module
    cluster_override_init();  // v2.2: configurable cluster display override
  }
  serial_proto_init();
  serial_proto_set_mode(mode_name(currentMode));

  bootStartMs = millis();
  Serial.print("[main] Entering ");
  Serial.println(mode_name(currentMode));
}

// =============================================================
//  TX Motor_09 override (only in BOOST mode)
// =============================================================
static void tx_motor_09_if_due(uint32_t now) {
  const Settings& s = settings_get();
  if (!s.tx_enabled) return;
  // SAFETY: never TX during BOOT listen-only phase or in SAFE_FAULT,
  // regardless of force_tx_always. This protects the bus on first power-on.
  if (currentMode == MODE_BOOT)       return;
  if (currentMode == MODE_SAFE_FAULT) return;
  // Normally TX only in BOOST. If force_tx_always is set, TX in SILENT too
  // (diagnostic mode for bench testing on P/R/D).
  if (currentMode != MODE_BOOST && !s.force_tx_always) return;

  uint32_t period_ms = 1000 / max(1, (int)s.tx_rate_hz);
  if (now - lastTxMs < period_ms) return;

  // Compute coolant byte from current MAP
  float map = obd2_get_last_map_mbar();
  if (map < 0.0f) {
    // No MAP yet — fall back to min (cold needle)
    map = s.map_min_mbar;
  }
  uint8_t byte0 = coolant_map_mbar_to_byte(
    map, s.map_min_mbar, s.map_max_mbar
  );
  lastCoolantByte = byte0;
  serial_proto_set_coolant_byte(byte0);

  // Build Motor_09 frame
  CanFrame f;
  f.id  = s.cluster_motor09_id;
  f.len = 8;
  coolant_build_motor_09(byte0, f.data);

  bool ok = can_send(CAN_CLUSTER, f);
  if (ok) {
    serial_proto_report_tx(CAN_CLUSTER, f);
  }
  lastTxMs = now;
}

// =============================================================
//  Update state machine based on lever + boot timing
// =============================================================
static void update_mode(uint32_t now) {
  if (currentMode == MODE_SAFE_FAULT) return;

  const Settings& s = settings_get();
  if (!s.tx_enabled && currentMode != MODE_SAFE_FAULT) {
    // TX globally disabled by user — force silent-like behavior
    // (but we keep state machine so we can resume)
  }

  // Honor BOOT phase: 5s listen-only
  if (currentMode == MODE_BOOT) {
    if (s.listen_only_boot && (now - bootStartMs < 5000)) {
      return;  // still booting
    }
    currentMode = MODE_SILENT;
    Serial.println("[main] BOOT complete -> SILENT (listening)");
  }

  Mode want = currentMode;
  if (lever_is_boost_mode()) {
    want = MODE_BOOST;
  } else if (lever_get() != '?') {
    want = MODE_SILENT;
  }

  if (want != currentMode) {
    Serial.print("[main] Mode change: ");
    Serial.print(mode_name(currentMode));
    Serial.print(" -> ");
    Serial.println(mode_name(want));
    currentMode = want;
    serial_proto_set_mode(mode_name(currentMode));
  }
}

// =============================================================
//  LED indicator (status)
// =============================================================
static void update_led(uint32_t now) {
  // BOOT: solid ON
  // SILENT: slow blink (1 Hz)
  // BOOST: fast blink (5 Hz)
  // SAFE_FAULT: very fast blink (10 Hz)
  static uint32_t lastToggle = 0;
  static bool ledState = HIGH;
  uint32_t interval;
  switch (currentMode) {
    case MODE_BOOT:        digitalWrite(PIN_LED_STATUS, HIGH); return;
    case MODE_SILENT:      interval = 500; break;
    case MODE_BOOST:       interval = 100; break;
    case MODE_BENCH_TEST:  interval = 200; break;  // medium blink (active bench)
    case MODE_SAFE_FAULT:  interval = 50;  break;
    default:               interval = 1000;
  }
  if (now - lastToggle >= interval) {
    ledState = !ledState;
    digitalWrite(PIN_LED_STATUS, ledState);
    lastToggle = now;
  }
}

// =============================================================
//  Main loop
// =============================================================
void loop() {
  uint32_t now = millis();

  // 1. Drain CAN RX queues + dispatch to listeners
  can_poll();

  // 2. Bench test mode — preempts normal logic if enabled.
  //    When bench is on, we emit the full sister-project bundle to test
  //    a standalone cluster (no vehicle). Normal Motor_09 TX is suppressed.
  bool bench_active = bench_test_tick();
  if (bench_active) {
    if (currentMode != MODE_BENCH_TEST) {
      currentMode = MODE_BENCH_TEST;
      serial_proto_set_mode("BENCH_TEST");
      Serial.println("[main] Bench test mode active");
    }
  } else {
    // 3. OBD2 periodic UDS query (in BOOST mode, or always if force_tx_always)
    obd2_tick(currentMode == MODE_BOOST || settings_get().force_tx_always);

    // 4. Update state machine from lever + boot timer
    if (currentMode == MODE_BENCH_TEST) {
      // Coming out of bench — back to BOOT cycle
      currentMode = MODE_BOOT;
      bootStartMs = now;
      serial_proto_set_mode(mode_name(currentMode));
    }
    update_mode(now);

    // 5. TX Motor_09 if in BOOST mode and due
    tx_motor_09_if_due(now);

    // 5b. v2.2 — cluster display override (TX modified WBA_03 at 40Hz when trigger active)
    bool safe_to_tx = settings_get().tx_enabled
                      && currentMode != MODE_BOOT
                      && currentMode != MODE_SAFE_FAULT;
    cluster_override_tick(now, safe_to_tx);
  }

  // 6. Handle serial commands from PC (if USB connected)
  serial_proto_poll();
  serial_proto_tick();

  // 7. Status LED
  update_led(now);

  // 8. Small yield to keep watchdog happy
  delay(1);
}
