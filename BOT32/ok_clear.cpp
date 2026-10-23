/*
 * ok_clear — implementation (v4.7.0).
 */
#include "ok_clear.h"
#include "button_sniffer.h"
#include "obd2.h"

static uint32_t g_seen_events  = 0;
static uint32_t g_press_ms[OK_CLEAR_PRESSES];   // ring of the last press times
static uint8_t  g_head         = 0;              // next slot to write
static uint8_t  g_count        = 0;              // valid entries (<= OK_CLEAR_PRESSES)
static bool     g_cooldown     = false;
static uint32_t g_cooldown_ms  = 0;

static void reset_window() {
  g_head  = 0;
  g_count = 0;
}

void ok_clear_init() {
  g_seen_events = button_sniffer_ok_press_events();
  reset_window();
  g_cooldown = false;
}

void ok_clear_tick(uint32_t now) {
  uint32_t events = button_sniffer_ok_press_events();
  uint32_t fresh  = events - g_seen_events;
  g_seen_events   = events;

  if (g_cooldown) {
    if (now - g_cooldown_ms < OK_CLEAR_COOLDOWN_MS) return;   // presses ignored
    g_cooldown = false;
    reset_window();
  }

  while (fresh--) {
    g_press_ms[g_head] = now;
    g_head = (g_head + 1) % OK_CLEAR_PRESSES;
    if (g_count < OK_CLEAR_PRESSES) g_count++;
  }

  if (g_count < OK_CLEAR_PRESSES) return;
  // Ring full: g_head now points at the oldest of the last 10 presses.
  if (now - g_press_ms[g_head] > OK_CLEAR_WINDOW_MS) return;

  Serial.print("[ok_clear] OK x");
  Serial.print(OK_CLEAR_PRESSES);
  Serial.print(" in < 6 s (code 0x");
  Serial.print(button_sniffer_ok_last_code(), HEX);
  Serial.println(") -> clear engine codes");
  if (!obd2_clear_engine_start("bouton OK")) {
    Serial.println("[ok_clear] clear refused (already running)");
  }
  g_cooldown    = true;
  g_cooldown_ms = now;
  reset_window();
}

uint8_t ok_clear_presses_in_window() {
  // Count entries still inside the window, relative to the newest press.
  if (g_count == 0) return 0;
  uint32_t now = millis();
  uint8_t n = 0;
  for (uint8_t i = 0; i < g_count; i++) {
    if (now - g_press_ms[i] <= OK_CLEAR_WINDOW_MS) n++;
  }
  return n;
}
