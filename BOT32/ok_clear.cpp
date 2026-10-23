/*
 * ok_clear — implementation (v4.8.0: hold OK 6 s).
 */
#include "ok_clear.h"
#include "button_sniffer.h"
#include "obd2.h"
#include "config.h"

static uint32_t g_seen_events  = 0;
static bool     g_holding      = false;   // a hold is being timed
static uint32_t g_hold_start   = 0;
static uint32_t g_hold_now     = 0;
static bool     g_cooldown     = false;
static uint32_t g_cooldown_ms  = 0;

void ok_clear_init() {
  g_seen_events = button_sniffer_ok_press_events();
  g_holding  = false;
  g_cooldown = false;
}

void ok_clear_tick(uint32_t now) {
  g_hold_now = now;
  // A new press edge (released -> pressed, counted per frame) starts a hold.
  uint32_t events = button_sniffer_ok_press_events();
  if (events != g_seen_events) {
    g_seen_events = events;
    g_holding     = true;
    g_hold_start  = now;
  }

  // Held = pressed AND frames still fresh (a lost bus ends the hold).
  bool pressed = button_sniffer_ok_pressed() &&
                 button_sniffer_ok_age_ms() < MFSW_STALE_TIMEOUT_MS;
  if (!pressed) { g_holding = false; return; }

  if (g_cooldown) {
    if (now - g_cooldown_ms < OK_CLEAR_COOLDOWN_MS) { g_holding = false; return; }
    g_cooldown = false;
  }

  if (!g_holding || (now - g_hold_start) < OK_CLEAR_HOLD_MS) return;

  Serial.print("[ok_clear] OK held 6 s (code 0x");
  Serial.print(button_sniffer_ok_last_code(), HEX);
  Serial.println(") -> clear engine codes");
  if (!obd2_clear_engine_start("bouton OK")) {
    Serial.println("[ok_clear] clear refused (already running)");
  }
  g_holding     = false;            // one trigger per hold: release before the next
  g_cooldown    = true;
  g_cooldown_ms = now;
}

uint32_t ok_clear_hold_ms() {
  return g_holding ? (g_hold_now - g_hold_start) : 0;
}
