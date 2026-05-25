/*
 * mode_state.cpp — implementation
 */
#include "mode_state.h"
#include "config.h"

static uint8_t  g_mode = 0;            // start in STOCK
static uint32_t g_mode_set_ms = 0;     // when current non-stock mode was entered

void mode_state_init() {
  g_mode = 0;
  g_mode_set_ms = 0;
}

void mode_state_set(uint8_t new_mode) {
  if (new_mode > 5) new_mode = 0;
  if (new_mode != g_mode) {
    Serial.print("[mode] ");
    Serial.print(mode_state_name(g_mode));
    Serial.print(" -> ");
    Serial.println(mode_state_name(new_mode));
  }
  g_mode = new_mode;
  g_mode_set_ms = millis();
}

uint8_t mode_state_get() {
  return g_mode;
}

const char* mode_state_name(uint8_t mode) {
  switch (mode) {
    case 0: return "STOCK";
    case 1: return "FWD";
    case 2: return "5050";
    case 3: return "6040";
    case 4: return "7525";
    case 5: return "EXPERT";
    default: return "?";
  }
}

void mode_state_tick() {
  if (g_mode == 0) return;                 // STOCK: nothing to revert
  if (g_mode == 5) return;                 // EXPERT: user/MITM controls; no auto-revert

  uint32_t now = millis();
  uint32_t elapsed = now - g_mode_set_ms;
  uint32_t timeout_ms = AUTO_REVERT_OTHER_MS;  // default for 6040/7525

  if (g_mode == 1)      timeout_ms = AUTO_REVERT_FWD_MS;
  else if (g_mode == 2) timeout_ms = AUTO_REVERT_5050_MS;

  if (timeout_ms == 0) return;             // 0 = disabled in config

  if (elapsed >= timeout_ms) {
    Serial.print("[mode] auto-revert ");
    Serial.print(mode_state_name(g_mode));
    Serial.print(" -> STOCK (after ");
    Serial.print(elapsed);
    Serial.println(" ms)");
    g_mode = 0;
    g_mode_set_ms = now;
  }
}
