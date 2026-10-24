/*
 * ignition_watch — implementation (v4.8.0).
 */
#include "ignition_watch.h"
#include "button_sniffer.h"
#include "can_handler.h"
#include "haldex_modes.h"
#include <esp_system.h>   // esp_reset_reason()

static bool     g_boot_power_on = false;  // full power-up: STOCK on the first tick
static bool     g_kl15_known = false;
static bool     g_kl15_last  = false;
static bool     g_cut        = false;     // ignition OFF or bus silent
static uint32_t g_cut_start  = 0;
static bool     g_cut_forced = false;     // long-cut STOCK already sent for this cut
static bool     g_bus_back   = false;     // cluster bus active again after a silence
static uint32_t g_bus_back_ms = 0;

static void cut_begin(uint32_t t) {
  if (g_cut) return;
  g_cut        = true;
  g_cut_start  = t;
  g_cut_forced = false;
}

static void cut_end(uint32_t now) {
  if (!g_cut) return;
  g_cut = false;
  bool long_cut = (int32_t)(now - g_cut_start) >= (int32_t)IGN_LONG_CUT_MS;
  // Normally already sent (and confirmed) when the cut reached 5 min.
  if (long_cut && !(g_cut_forced && haldex_modes_force_stock_confirmed())) {
    haldex_modes_force_stock("contact mis apres une longue coupure");
  }
}

void ignition_watch_init() {
  g_boot_power_on = (esp_reset_reason() == ESP_RST_POWERON);
  g_kl15_known = false;
  g_kl15_last  = false;
  g_cut        = false;
  g_cut_forced = false;
  g_bus_back   = false;
}

void ignition_watch_tick(uint32_t now) {
  if (g_boot_power_on) {
    g_boot_power_on = false;
    haldex_modes_force_stock("mise sous tension BOT32");
  }

  // Cluster bus silent. Silence is measured between the last frame and the last
  // can_poll() (not the current clock), so a long stall elsewhere in loop() —
  // e.g. a blocked HTTP request — never looks like a silent bus.
  CanStats cs = can_get_stats(CAN_CLUSTER);
  uint32_t silent;
  if (cs.last_rx_ms == 0) {
    silent = UINT32_MAX;                 // nothing received since boot
  } else {
    int32_t d = (int32_t)(can_last_poll_ms() - cs.last_rx_ms);
    silent = d < 0 ? 0 : (uint32_t)d;
  }
  if (silent >= IGN_BUS_SILENT_MS) {
    // The cut started with the last frame (or now if none since boot).
    bool past = cs.last_rx_ms != 0 && (int32_t)(now - cs.last_rx_ms) >= 0;
    cut_begin(past ? cs.last_rx_ms : now);
    g_kl15_known = false;                // re-learn the ignition state on wake
    g_bus_back   = false;
  } else if (!g_bus_back) {
    g_bus_back    = true;
    g_bus_back_ms = now;
  }

  // Ignition (fresh Klemmen_Status_01 frames only).
  uint32_t kl_age = button_sniffer_ignition_age_ms();
  if (kl_age < IGN_FRESH_MS) {
    bool on = button_sniffer_ignition_on();
    if (!g_kl15_known || on != g_kl15_last) {
      g_kl15_known = true;
      g_kl15_last  = on;
      if (on) cut_end(now);
      else    cut_begin(now);
    }
  } else if (kl_age == UINT32_MAX && g_bus_back &&
             (int32_t)(now - g_bus_back_ms) >= (int32_t)(2 * IGN_FRESH_MS)) {
    cut_end(now);                        // no 0x3C0 on this bus: bus active = ON
  }

  // A cut that lasts 5 min: STOCK now, while the X2 is still awake.
  if (g_cut && !g_cut_forced && (int32_t)(now - g_cut_start) >= (int32_t)IGN_LONG_CUT_MS) {
    g_cut_forced = true;
    haldex_modes_force_stock("coupure de 5 min");
  }
}
