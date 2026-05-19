/*
 * BOT32 — In-vehicle boost-on-coolant override for VW MK7 cluster.
 *
 * Architecture:
 *   - CAN0 (TWAI internal) → MK7 cluster bus (TX Motor_09, RX WBA_03)
 *   - CAN1 (MCP2515 SPI)   → OBD-II port (UDS query DID 0x39C0 for MAP)
 *   - USB serial @ 115200 → debug/config interface to PC web UI
 *   - NVS storage         → persistent user settings
 *
 * State machine:
 *   BOOT       (5s listen-only at startup)
 *   SILENT     (lever P/R/D: no TX, cluster shows real coolant)
 *   BOOST      (lever S/M/N: poll MAP via OBD2, TX Motor_09 override)
 *   SAFE_FAULT (TX disabled by user or fatal error: no TX)
 *
 * Libraries required (install via Arduino IDE Library Manager):
 *   - ACAN2515       (Pierre Molinaro)
 *   - ArduinoJson    (Benoit Blanchon)
 */
#include "config.h"
#include "vw_mqb.h"
#include "coolant.h"
#include "can_handler.h"
#include "obd2.h"
#include "lever_decoder.h"
#include "settings.h"
#include "serial_proto.h"

// =============================================================
//  State
// =============================================================
enum Mode {
  MODE_BOOT,
  MODE_SILENT,
  MODE_BOOST,
  MODE_SAFE_FAULT,
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
  }
  serial_proto_init();

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
  if (currentMode != MODE_BOOST) return;

  uint32_t period_ms = 1000 / max(1, (int)s.tx_rate_hz);
  if (now - lastTxMs < period_ms) return;

  // Compute coolant byte from current MAP
  float map = obd2_get_last_map_mbar();
  if (map < 0.0f) {
    // No MAP yet — fall back to min (cold needle)
    map = s.map_min_mbar;
  }
  uint8_t byte0 = coolant_map_mbar_to_byte(
    map, s.map_min_mbar, s.map_max_mbar, s.scale, s.offset_c
  );
  lastCoolantByte = byte0;

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

  // 2. OBD2 periodic UDS query (only when in BOOST, to spare bus when not needed)
  obd2_tick(currentMode == MODE_BOOST);

  // 3. Update state machine from lever + boot timer
  update_mode(now);

  // 4. TX Motor_09 if in BOOST mode and due
  tx_motor_09_if_due(now);

  // 5. Handle serial commands from PC (if USB connected)
  serial_proto_poll();
  serial_proto_tick();

  // 6. Status LED
  update_led(now);

  // 7. Small yield to keep watchdog happy
  delay(1);
}
