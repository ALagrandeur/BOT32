/*
 * BOT32 — In-vehicle boost-on-coolant override for VW MK7 cluster.
 *
 * Hardware: ESP32 with 2 CAN interfaces (see docs/hardware.md)
 * Framework: Arduino IDE
 *
 * Behavior:
 *   - Listen for gear lever position on CAN0 (WBA_03 0x394)
 *   - If lever in {S, M, N}: query MAP via OBD2 on CAN1, override coolant
 *   - If lever in {P, R, D}: stay silent, cluster shows real coolant
 *
 * Safety: Airbag IDs (0x040, 0x572) hardcoded in blocklist.
 *         Listen-only at boot until signals validated.
 */

#include "config.h"
#include "vw_mqb.h"
// #include "can_handler.h"     // TODO
// #include "obd2.h"             // TODO

// =============================================================
//  State
// =============================================================
enum Mode {
  MODE_BOOT,        // initial, listen-only
  MODE_SILENT,      // lever in P/R/D, no TX
  MODE_BOOST,       // lever in S/M/N, TX Motor_09 override
  MODE_SAFE_FAULT,  // something invalid, fall back to silent
};

Mode currentMode = MODE_BOOT;
unsigned long lastLeverRx = 0;
unsigned long lastMapRx   = 0;
unsigned long lastTxMs    = 0;

float lastMapMbar = 0.0f;
uint8_t lastCoolantByte = 0x80;  // 50°C default
char lastLever = '?';            // P, R, N, D, S, M

// =============================================================
//  Setup
// =============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("================================");
  Serial.println("  BOT32 v0.1 - boost override");
  Serial.println("================================");

  // TODO: init CAN0 (cluster bus, 500 kbps)
  // TODO: init CAN1 (OBD2 bus, 500 kbps)
  // TODO: pin LED status

  Serial.println("Setup done. Entering MODE_BOOT (listen-only).");
}

// =============================================================
//  Main loop
// =============================================================
void loop() {
  unsigned long now = millis();

  // 1. Read incoming CAN0 frames (lever + cluster Motor_09 sniff)
  // TODO: poll can0_rx(), parse WBA_03, sniff Motor_09 byte 0

  // 2. Read incoming CAN1 frames (OBD2 response)
  // TODO: poll can1_rx(), parse UDS response if expecting one

  // 3. Periodic OBD2 query (only in BOOST mode)
  if (currentMode == MODE_BOOST && now - lastTxMs > OBD2_POLL_INTERVAL_MS) {
    // TODO: send UDS ReadDataByIdentifier 0x39C0
    lastTxMs = now;
  }

  // 4. State machine: switch mode based on lever
  Mode newMode = currentMode;
  switch (lastLever) {
    case 'P': case 'R': case 'D':
      newMode = MODE_SILENT;
      break;
    case 'S': case 'M': case 'N':
      newMode = MODE_BOOST;
      break;
    default:
      // Lever unknown -> stay in current mode (or BOOT)
      break;
  }
  if (newMode != currentMode) {
    Serial.print("Mode change: ");
    Serial.print(modeName(currentMode));
    Serial.print(" -> ");
    Serial.println(modeName(newMode));
    currentMode = newMode;
  }

  // 5. TX Motor_09 override (only in BOOST mode, @ 20 Hz)
  // TODO: build Motor_09 payload with lastCoolantByte, send on CAN0

  // 6. Status LED + serial debug every 1 sec
  static unsigned long lastStatus = 0;
  if (now - lastStatus > 1000) {
    printStatus();
    lastStatus = now;
  }
}

// =============================================================
//  Helpers
// =============================================================
const char* modeName(Mode m) {
  switch (m) {
    case MODE_BOOT:       return "BOOT";
    case MODE_SILENT:     return "SILENT";
    case MODE_BOOST:      return "BOOST";
    case MODE_SAFE_FAULT: return "SAFE_FAULT";
    default:              return "?";
  }
}

void printStatus() {
  Serial.print("[");
  Serial.print(modeName(currentMode));
  Serial.print("] lever=");
  Serial.print(lastLever);
  Serial.print(" MAP=");
  Serial.print(lastMapMbar, 0);
  Serial.print(" mbar  coolantByte=0x");
  Serial.print(lastCoolantByte, HEX);
  Serial.println();
}
