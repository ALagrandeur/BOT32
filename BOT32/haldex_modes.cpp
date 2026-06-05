/*
 * haldex_modes.cpp — main-side Haldex mode decision logic (v3.1.0).
 *
 * Decides the active mode (STOCK/FWD/5050) from two sources and pushes it to
 * the external MITM (ESP32-CAN-X2) via the ESP-NOW link:
 *
 *   - FWD  : PHYSICAL combo = Hazards ON + Traction-Control button, detected
 *            via the existing cluster sniffers. Exits (-> STOCK) when the
 *            hazards are turned OFF. Also settable from app/USB.
 *   - 5050 : app/USB button only. Exits via the app/USB STOCK button.
 *   - STOCK: default; app/USB button; or hazards-OFF after a combo FWD.
 *
 * NOTE on "TC OFF": the true "traction control disabled" latched state lives
 * in ESP_21 (0x0FD) byte6 flag bits. For now we use the TC *button* press
 * (the sniffer we already validated) while hazards are ON as the arming
 * gesture — easy and deliberate. We can switch to the latched ESP flag after
 * bench confirmation without changing the public API.
 *
 * No timed auto-revert (a timed mechanical revert was judged too dangerous).
 */
#include "haldex_modes.h"
#include "button_sniffer.h"
#include "haldex_link.h"
#include "settings.h"
#include "config.h"        // CAN_ID_WBA_03
#include "can_handler.h"   // can_send on the cluster bus
#include "vw_mqb.h"        // mqb_apply (counter + CRC for 0x394)

#define COMBO_FRESH_MS    2000   // sniffer values older than this are ignored
#define TELLTALE_BLINK_MS 500    // 0.5 s blink phase while non-STOCK
#define TELLTALE_TX_MS    25     // v3.10.0: 40 Hz TX to dominate the gateway's ~20 Hz

static uint8_t  g_mode          = HALDEX_M_STOCK;
static bool     g_fwd_from_combo = false;   // FWD currently held by the combo
static bool     g_prev_combo     = false;   // rising-edge detector
static bool     g_telltale_phase = false;
static uint32_t g_last_blink_ms  = 0;
static bool     g_passthrough_desired = true;  // safe default (X2 boots ON too)
static uint32_t g_last_tt_tx_ms  = 0;          // v3.10.0: FWD telltale TX rate-limit
static uint8_t  g_tt_counter     = 0;          // v3.10.0: WBA_03 rolling counter

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

  // ---- Physical combo: Hazards ON + TC button (both fresh) ----
  bool hazard_on = button_sniffer_hazard_active() &&
                   (button_sniffer_hazard_age_ms() < COMBO_FRESH_MS);
  bool tc_press  = button_sniffer_tc_pressed() &&
                   (button_sniffer_tc_age_ms() < COMBO_FRESH_MS);
  bool combo     = hazard_on && tc_press;

  // Rising edge of the combo arms FWD.
  if (combo && !g_prev_combo) {
    g_fwd_from_combo = true;
    apply_mode(HALDEX_M_FWD);
  }
  g_prev_combo = combo;

  // Hazards OFF releases a combo-armed FWD back to STOCK.
  if (g_fwd_from_combo && !hazard_on) {
    g_fwd_from_combo = false;
    apply_mode(HALDEX_M_STOCK);
  }

  // ---- Telltale blink phase ----
  if (g_mode == HALDEX_M_STOCK) {
    g_telltale_phase = false;
  } else if (now - g_last_blink_ms >= TELLTALE_BLINK_MS) {
    g_telltale_phase = !g_telltale_phase;
    g_last_blink_ms = now;
  }

  // ---- v3.10.0: optional FWD cluster telltale (opt-in) ----
  // When haldex_fwd_telltale is ON and mode == FWD, blink the WBA_03 (0x394)
  // "shift lock — press brake" message on the km/h dial as a visual FWD
  // indicator. We TX at ~40 Hz during the blink-ON phase to DOMINATE the
  // gateway's ~20 Hz (same technique the old cluster display override used),
  // and stay silent during the OFF phase so the gateway's normal gear display
  // resumes → the message blinks. Default OFF (TXing on the cluster bus contends
  // with the gateway → closed-course / bench only). Honours the safety rule:
  // nothing fires unless the user explicitly enables this setting.
  if (s.haldex_fwd_telltale && g_mode == HALDEX_M_FWD && g_telltale_phase) {
    if (now - g_last_tt_tx_ms >= TELLTALE_TX_MS) {
      g_last_tt_tx_ms = now;
      // Data bytes from the bench capture that lights the message. byte0 (CRC)
      // and byte1 low nibble (rolling counter) are filled by mqb_apply().
      uint8_t p[8] = { 0x00, 0x10, 0x00, 0x1A, 0x00, 0x00, 0x00, 0x00 };
      mqb_apply(CAN_ID_WBA_03, p, 8, g_tt_counter);
      g_tt_counter = (uint8_t)((g_tt_counter + 1) & 0x0F);
      CanFrame f;
      f.id = CAN_ID_WBA_03;
      f.len = 8;
      f.timestamp = now;
      for (uint8_t b = 0; b < 8; b++) f.data[b] = p[b];
      can_send(CAN_CLUSTER, f);
    }
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
