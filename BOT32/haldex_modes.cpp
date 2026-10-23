/*
 * haldex_modes.cpp — main-side Haldex mode decision logic (v3.1.0).
 *
 * Decides the active mode (STOCK/FWD/5050) from two sources and pushes it to
 * the external MITM (ESP32-CAN-X2) via the ESP-NOW link:
 *
 *   - FWD  : PHYSICAL combo = Hazards ON + Traction-Control OFF, detected via
 *            the existing cluster sniffers. v3.10.0: exits (-> STOCK) when
 *            traction control is turned back ON (NOT when the hazards go off),
 *            so the hazards can be switched off while FWD stays armed. Also
 *            settable from app/USB.
 *   - 5050 : v4.6.0: same combo with the HAND BRAKE PULLED at the moment the
 *            combo completes (else FWD); exits like FWD (TC back ON). Also
 *            app/USB button, which exits via the app/USB STOCK button.
 *   - STOCK: default; app/USB button; or TC-back-ON after a combo FWD.
 *
 * NOTE on "TC OFF": confirmed on the live data that ESP_21 (0x0FD) byte6 == 0x03
 * is the LATCHED "traction control disabled" state — it stays set after the
 * button is released, and clears (0x00) when TC is re-enabled. So we use it as
 * BOTH the arming gesture (with hazards ON) and the FWD hold/release condition:
 * FWD is held while TC is OFF and released when TC is turned back ON.
 *
 * No timed auto-revert (a timed mechanical revert was judged too dangerous).
 */
#include "haldex_modes.h"
#include "button_sniffer.h"
#include "haldex_link.h"
#include "settings.h"
#include "config.h"
#include "can_handler.h"

#define COMBO_FRESH_MS    2000   // sniffer values older than this are ignored
#define COMBO_POLL_STEP_MS 100   // v4.8.0: max polled time one can_poll() can add
#define TELLTALE_BLINK_MS 500    // 0.5 s blink phase while non-STOCK

static uint8_t  g_mode          = HALDEX_M_STOCK;
static bool     g_mode_from_combo = false;  // FWD/50-50 currently held by the combo
static bool     g_prev_combo     = false;   // rising-edge detector
static bool     g_telltale_phase = false;
static uint32_t g_last_blink_ms  = 0;
static bool     g_passthrough_desired = true;  // safe default (X2 boots ON too)
// v4.8.0 — forced-STOCK re-send window (see haldex_modes_force_stock)
#define FORCE_STOCK_WINDOW_MS  10000
#define FORCE_STOCK_RESEND_MS    500
static bool     g_force_pending  = false;
static uint32_t g_force_start_ms = 0;
static uint32_t g_force_next_ms  = 0;
// v4.8.0 — polled time since each combo frame was last seen (see track_frame)
static uint32_t g_prev_poll_ms   = 0;
static uint32_t g_hz_missed_ms   = COMBO_FRESH_MS;
static uint32_t g_tc_missed_ms   = COMBO_FRESH_MS;
static uint32_t g_hb_missed_ms   = COMBO_FRESH_MS;

void haldex_modes_init() {
  g_mode           = HALDEX_M_STOCK;
  g_mode_from_combo = false;
  g_prev_combo     = false;
  g_telltale_phase = false;
  g_last_blink_ms  = 0;
  g_passthrough_desired = true;
  g_prev_poll_ms   = 0;
  g_hz_missed_ms   = COMBO_FRESH_MS;
  g_tc_missed_ms   = COMBO_FRESH_MS;
  g_hb_missed_ms   = COMBO_FRESH_MS;
}

// Internal: change mode + forward to the MITM if it actually changed.
static void apply_mode(uint8_t mode) {
  if (mode > HALDEX_M_5050) mode = HALDEX_M_5050;
  if (mode != HALDEX_M_STOCK) g_force_pending = false;   // driver chose a mode
  if (mode == g_mode) return;
  g_mode = mode;
  haldex_link_set_mode(g_mode);   // ESP-NOW SET_MODE to the X2
  Serial.print("[haldex_modes] mode -> ");
  Serial.println(g_mode);
}

bool haldex_modes_set_manual(uint8_t mode) {
  const Settings& s = settings_get();
  if (!s.haldex_enabled) return false;
  if (mode > HALDEX_M_5050) return false;

  // Manual command clears the combo latch so the two sources don't fight.
  g_mode_from_combo = false;
  // An explicit STOCK is always (re)sent: the X2 may still be in FWD/50-50 while
  // g_mode already reads STOCK (lost broadcast, BOT32 rebooted, USB-C dev link).
  if (mode == HALDEX_M_STOCK && g_mode == HALDEX_M_STOCK) {
    haldex_link_set_mode(HALDEX_M_STOCK);
  }
  apply_mode(mode);
  return true;
}

void haldex_modes_force_stock(const char* reason) {
  const Settings& s = settings_get();
  if (!s.haldex_enabled) return;
  uint32_t now = millis();
  g_mode_from_combo = false;
  apply_mode(HALDEX_M_STOCK);
  haldex_link_set_mode(HALDEX_M_STOCK);       // always (re)send, even if g_mode was STOCK
  g_force_pending  = true;
  g_force_start_ms = now;
  g_force_next_ms  = now + FORCE_STOCK_RESEND_MS;
  Serial.print("[haldex_modes] forced STOCK: ");
  Serial.println(reason ? reason : "");
}

bool haldex_modes_force_stock_pending() { return g_force_pending; }

// v4.8.0 — combo frames go stale on POLLED time, not wall time. After a long
// stall elsewhere in loop() (e.g. a blocked HTTP request) the 32-frame MCP2515
// RX buffer has overflowed and the first can_poll() may hold no TC frame at
// all; that poll adds at most COMBO_POLL_STEP_MS, so the last decoded value is
// kept until normal polling has missed the frame for COMBO_FRESH_MS. A real bus
// loss (polls continue, no frames) still goes stale after COMBO_FRESH_MS.
static bool seen_at_last_poll(uint32_t age_now) {
  if (age_now == UINT32_MAX) return false;
  int32_t since_poll = (int32_t)(millis() - can_last_poll_ms());
  return age_now <= (since_poll < 0 ? 0u : (uint32_t)since_poll);
}

static void track_frame(uint32_t& missed_ms, uint32_t age_now, uint32_t step) {
  if (seen_at_last_poll(age_now))     missed_ms = 0;
  else if (missed_ms < COMBO_FRESH_MS) missed_ms += step;
}

static void track_combo_frames() {
  uint32_t poll = can_last_poll_ms();
  uint32_t step = 0;
  if (g_prev_poll_ms != 0) {
    int32_t d = (int32_t)(poll - g_prev_poll_ms);
    step = d <= 0 ? 0 : (d > COMBO_POLL_STEP_MS ? COMBO_POLL_STEP_MS : (uint32_t)d);
  }
  g_prev_poll_ms = poll;
  track_frame(g_hz_missed_ms, button_sniffer_hazard_age_ms(),    step);
  track_frame(g_tc_missed_ms, button_sniffer_tc_age_ms(),        step);
  track_frame(g_hb_missed_ms, button_sniffer_handbrake_age_ms(), step);
}

static void force_stock_tick(uint32_t now) {
  if (!g_force_pending) return;
  HaldexState hx = haldex_link_get_state();
  if (hx.valid && haldex_link_get_age_ms() < HALDEX_LINK_STALE_MS &&
      hx.current_mode == HALDEX_M_STOCK &&
      (int32_t)(now - g_force_start_ms) >= (int32_t)FORCE_STOCK_RESEND_MS) {
    g_force_pending = false;                   // X2 confirmed STOCK
    Serial.println("[haldex_modes] X2 confirmed STOCK");
    return;
  }
  if ((int32_t)(now - g_force_start_ms) >= (int32_t)FORCE_STOCK_WINDOW_MS) {
    g_force_pending = false;                   // give up re-sending (X2 off/asleep)
    return;
  }
  if ((int32_t)(now - g_force_next_ms) >= 0) {
    haldex_link_set_mode(HALDEX_M_STOCK);
    g_force_next_ms = now + FORCE_STOCK_RESEND_MS;
  }
}

void haldex_modes_tick(uint32_t now) {
  const Settings& s = settings_get();

  force_stock_tick(now);
  track_combo_frames();

  if (!s.haldex_enabled) {
    g_force_pending = false;
    // Link disabled: force STOCK once and do nothing else.
    if (g_mode != HALDEX_M_STOCK) apply_mode(HALDEX_M_STOCK);
    g_mode_from_combo = false;
    g_prev_combo = false;
    g_telltale_phase = false;
    return;
  }

  // ---- Physical combo: Hazards ON + TC OFF (both fresh) ----
  // button_sniffer_tc_pressed() == the LATCHED traction-control-disabled state
  // (0x0FD byte6==0x03 stays set while TC is off), so tc_off is a stable signal.
  bool hazard_on = button_sniffer_hazard_active() && g_hz_missed_ms < COMBO_FRESH_MS;
  bool tc_off    = button_sniffer_tc_pressed()    && g_tc_missed_ms < COMBO_FRESH_MS;
  bool combo     = hazard_on && tc_off;

  // Rising edge of the combo arms FWD, or 50/50 if the hand brake is pulled at
  // that instant (KOMBI_01 0x30B, vehicle-confirmed). Only arms from a
  // non-combo state: once a combo mode is held, re-pressing the hazards (or a
  // stale 0x366 frame coming back) never switches FWD <-> 50/50 while TC is
  // still OFF. To change, turn TC back ON (-> STOCK) and redo the combo.
  // Hazards ON is the deliberate gate, so disabling TC alone never arms anything.
  if (combo && !g_prev_combo && !g_mode_from_combo) {
    bool hb_on = button_sniffer_handbrake_active() && g_hb_missed_ms < COMBO_FRESH_MS;
    g_mode_from_combo = true;
    apply_mode(hb_on ? HALDEX_M_5050 : HALDEX_M_FWD);
  }
  g_prev_combo = combo;

  // v3.10.0: TC turned back ON releases a combo-armed mode (FWD or 50/50) back to STOCK (was
  // "hazards OFF"). So the hazards can be switched off while FWD stays armed;
  // FWD drops only when traction control is re-enabled. Losing the TC frame
  // (stale -> tc_off=false) also drops to STOCK, a safe default.
  if (g_mode_from_combo && !tc_off) {
    g_mode_from_combo = false;
    apply_mode(HALDEX_M_STOCK);
  }

  // ---- Telltale blink phase (exposed only; no CAN emit here) ----
  if (g_mode == HALDEX_M_STOCK) {
    g_telltale_phase = false;
  } else if (now - g_last_blink_ms >= TELLTALE_BLINK_MS) {
    g_telltale_phase = !g_telltale_phase;
    g_last_blink_ms = now;
  }
}

uint8_t haldex_modes_get()          { return g_mode; }
bool    haldex_modes_telltale_on()  { return g_telltale_phase; }

bool haldex_modes_set_passthrough(bool passthrough) {
  const Settings& s = settings_get();
  if (!s.haldex_enabled) return false;
  bool ok = haldex_link_set_passthrough(passthrough);
  if (ok) g_passthrough_desired = passthrough;
  return ok;
}

bool haldex_modes_get_passthrough_desired() { return g_passthrough_desired; }
