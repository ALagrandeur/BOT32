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
static bool     g_releasing    = false;   // released, not yet for OK_RELEASE_DEBOUNCE_MS
static uint32_t g_release_ms   = 0;
static bool     g_cooldown     = false;
static uint32_t g_cooldown_ms  = 0;
static uint32_t g_last_hold_ms = 0;       // diagnostics: length of the last hold
static const char* g_last_end  = "";      // diagnostics: how it ended

static void end_hold(uint32_t now, const char* why) {
  if (g_holding) {
    g_last_hold_ms = now - g_hold_start;
    g_last_end     = why;
  }
  g_holding   = false;
  g_releasing = false;
}

void ok_clear_init() {
  g_seen_events = button_sniffer_ok_press_events();
  g_holding   = false;
  g_releasing = false;
  g_cooldown  = false;
}

void ok_clear_tick(uint32_t now) {
  g_hold_now = now;
  // A new press edge (released -> pressed, counted per frame) starts a hold;
  // during a hold, a short release (flicker, code change) does not restart it.
  uint32_t events = button_sniffer_ok_press_events();
  if (events != g_seen_events) {
    g_seen_events = events;
    if (!g_holding) {
      g_holding   = true;
      g_hold_start = now;
    }
  }
  if (!g_holding) return;

  // A lost bus ends the hold.
  if (button_sniffer_ok_age_ms() >= MFSW_STALE_TIMEOUT_MS) { end_hold(now, "trame perimee"); return; }

  // Released: the hold ends only once OK stays released for OK_RELEASE_DEBOUNCE_MS.
  if (!button_sniffer_ok_pressed()) {
    if (!g_releasing) { g_releasing = true; g_release_ms = now; }
    if ((int32_t)(now - g_release_ms) >= (int32_t)OK_RELEASE_DEBOUNCE_MS) end_hold(now, "relache");
    return;
  }
  g_releasing = false;

  if (g_cooldown) {
    if (now - g_cooldown_ms < OK_CLEAR_COOLDOWN_MS) { end_hold(now, "inhibition 10 s"); return; }
    g_cooldown = false;
  }

  if ((int32_t)(now - g_hold_start) < (int32_t)OK_CLEAR_HOLD_MS) return;

  Serial.print("[ok_clear] OK held 6 s (code 0x");
  Serial.print(button_sniffer_ok_last_code(), HEX);
  Serial.println(") -> clear engine codes");
  bool started = obd2_clear_engine_start("bouton OK");
  if (!started) Serial.println("[ok_clear] clear refused (already running)");
  end_hold(now, started ? "declenche" : "refuse (effacement en cours)");   // one trigger per hold
  g_cooldown    = true;
  g_cooldown_ms = now;
}

uint32_t ok_clear_hold_ms() {
  return g_holding ? (g_hold_now - g_hold_start) : 0;
}

uint32_t ok_clear_last_hold_ms() { return g_last_hold_ms; }
const char* ok_clear_last_end()  { return g_last_end; }
