/*
 * Version: v2.3.0 — https://github.com/ALagrandeur/BOT32/releases/tag/v2.3.0
 * BOT32-Haldex — Haldex bus MITM module
 *
 * Sister sketch to BOT32 main. Runs on a 2nd ESP32 + WaveShare 2-CH CAN HAT.
 * Sits physically between the PCM and the Haldex AWD controller (cut the
 * private Haldex CAN bus, route through both HAT terminal blocks).
 *
 * Responsibilities:
 *   1. Bridge frames between PCM side <-> Haldex controller side in tight
 *      loop for minimum latency.
 *   2. Listen for SET_MODE commands from BOT32 main over ESP-NOW.
 *   3. Apply mode-specific modifications to the Haldex demand frame
 *      (USER-IMPLEMENTED — see haldex_mitm.cpp STUBS).
 *   4. Periodically broadcast STATE over ESP-NOW so BOT32 main's web UI
 *      shows live data (current mode, pump %, target %, speed, pedal).
 *   5. Auto-revert race modes (FWD / 5050) to STOCK after timeout for safety.
 *
 * Hardware identical to BOT32 main:
 *   - ESP32 DevKit
 *   - WaveShare 2-CH CAN HAT (2x MCP2515 + 2x SIT65HVD230)
 *   - 9 Dupont F-F jumpers between HAT header and ESP32 GPIO
 *   - VIO jumper in 3V3 position
 *   - BOTH 120R terminator jumpers ON (we replace the OEM terminators we cut off)
 *
 * Required Arduino libraries (Library Manager):
 *   - ACAN2515 by Pierre Molinaro
 *
 * License: MIT (BOT32 project).
 *
 * NOTE: The actual Haldex bus protocol details (demand frame CAN ID,
 * byte layout, MQB CRC constants for the Haldex IDs) are NOT included in
 * this skeleton — they live in haldex_mitm.cpp as STUBS that you fill in
 * from your own sources (personal fork of OpenHaldex-C6, vehicle sniffing,
 * etc.). Until those stubs are filled in, BOT32-Haldex acts as a transparent
 * bridge (no AWD modification) — perfectly safe for initial hardware
 * validation.
 */
#include "config.h"
#include "can_handler.h"
#include "espnow_peer.h"
#include "mode_state.h"
#include "haldex_mitm.h"

static uint32_t g_last_status_print_ms = 0;
static uint32_t g_loop_iterations = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("====================================");
  Serial.println("  BOT32-Haldex — MITM module");
  Serial.println("====================================");

  pinMode(PIN_LED_STATUS, OUTPUT);
  digitalWrite(PIN_LED_STATUS, HIGH);

  // Init subsystems
  if (!can_init()) {
    Serial.println("[FATAL] CAN init failed. Check HAT wiring + VIO=3V3.");
    // Continue anyway so ESP-NOW can still report status
  }
  mode_state_init();
  haldex_mitm_init();
  espnow_peer_init();

  Serial.print("[setup] MAC = ");
  Serial.println(espnow_peer_get_my_mac());
  Serial.println("[setup] Ready. Bridging PCM <-> Haldex.");
}

void loop() {
  uint32_t now = millis();

  // 1. Drain PCM-side queue, optionally modify, forward to Haldex side.
  //    Tight loop to keep latency < 1 ms typically.
  CanFrame f;
  while (can_pcm_available()) {
    if (can_pcm_receive(f)) {
      if (haldex_mitm_process_pcm_frame(f)) {
        can_haldex_send(f);
      }
    }
  }

  // 2. Drain Haldex-side queue, extract state, forward to PCM side.
  while (can_haldex_available()) {
    if (can_haldex_receive(f)) {
      if (haldex_mitm_process_haldex_frame(f)) {
        can_pcm_send(f);
      }
    }
  }

  // 3. ESP-NOW state broadcast (periodic, internally rate-limited)
  espnow_peer_tick();

  // 4. Mode auto-revert (safety timer)
  mode_state_tick();

  // 5. Periodic status print on Serial (every 5s) — debug aid
  g_loop_iterations++;
  if (now - g_last_status_print_ms >= 5000) {
    CanSideStats p = can_pcm_stats();
    CanSideStats h = can_haldex_stats();
    Serial.print("[5s] mode=");
    Serial.print(mode_state_name(mode_state_get()));
    Serial.print(" pcm: rx=");  Serial.print(p.rx);
    Serial.print(" tx_ok=");   Serial.print(p.tx_ok);
    Serial.print(" tx_fail="); Serial.print(p.tx_fail);
    Serial.print(" | hdx: rx=");  Serial.print(h.rx);
    Serial.print(" tx_ok=");   Serial.print(h.tx_ok);
    Serial.print(" tx_fail="); Serial.print(h.tx_fail);
    Serial.print(" | loops=");  Serial.println(g_loop_iterations);
    g_last_status_print_ms = now;
    g_loop_iterations = 0;
  }

  // 6. LED heartbeat (slow blink while alive)
  static uint32_t last_led = 0;
  if (now - last_led >= 500) {
    digitalWrite(PIN_LED_STATUS, !digitalRead(PIN_LED_STATUS));
    last_led = now;
  }

  // Yield to keep watchdog happy
  delay(1);
}
