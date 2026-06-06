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
 *   - 5050 : app/USB button only. Exits via the app/USB STOCK button.
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

#define COMBO_FRESH_MS    2000   // sniffer values older than this are ignored
#define TELLTALE_BLINK_MS 500    // 0.5 s blink phase while non-STOCK

static uint8_t  g_mode          = HALDEX_M_STOCK;
static bool     g_fwd_from_combo = false;   // FWD currently held by the combo
static bool     g_prev_combo     = false;   // rising-edge detector
static bool     g_telltale_phase = false;
static uint32_t g_last_blink_ms  = 0;
static bool     g_passthrough_desired = true;  // safe default (X2 boots ON too)

void haldex_modes_init() {
  g_mode           = HALDEX_M_STOCK;
  g_fwd_from_combo = false;
  g_prev_combo     = false;
  g_telltale_phase = false;
  g_last_blink_ms  = 0;
  g_passthrough_desired = true;
}

// Internal: change mode + forward to the MITM if it actually changed.
static void apply_mode(uint8_t mode) {
  if (mode > HALDEX_M_5050) mode = HALDEX_M_5050;
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
  g_fwd_from_combo = false;
  apply_mode(mode);
  return true;
}

void haldex_modes_tick(uint32_t now) {
  const Settings& s = settings_get();

  if (!s.haldex_enabled) {
    // Link disabled: force STOCK once and do nothing else.
    if (g_mode != HALDEX_M_STOCK) apply_mode(HALDEX_M_STOCK);
    g_fwd_from_combo = false;
    g_prev_combo = false;
    g_telltale_phase = false;
    return;
  }

  // ---- Physical combo: Hazards ON + TC OFF (both fresh) ----
  // button_sniffer_tc_pressed() == the LATCHED traction-control-disabled state
  // (0x0FD byte6==0x03 stays set while TC is off), so tc_off is a stable signal.
  bool hazard_on = button_sniffer_hazard_active() &&
                   (button_sniffer_hazard_age_ms() < COMBO_FRESH_MS);
  bool tc_off    = button_sniffer_tc_pressed() &&
                   (button_sniffer_tc_age_ms() < COMBO_FRESH_MS);
  bool combo     = hazard_on && tc_off;

  // Rising edge of the combo arms FWD. Hazards ON is the deliberate gate, so
  // disabling TC alone (no hazards) never arms FWD.
  if (combo && !g_prev_combo) {
    g_fwd_from_combo = true;
    apply_mode(HALDEX_M_FWD);
  }
  g_prev_combo = combo;

  // v3.10.0: TC turned back ON releases a combo-armed FWD back to STOCK (was
  // "hazards OFF"). So the hazards can be switched off while FWD stays armed;
  // FWD drops only when traction control is re-enabled. Losing the TC frame
  // (stale -> tc_off=false) also drops to STOCK, a safe default.
  if (g_fwd_from_combo && !tc_off) {
    g_fwd_from_combo = false;
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
