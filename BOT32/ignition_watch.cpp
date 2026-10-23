/*
 * ignition_watch — implementation (v4.8.0).
 */
#include "ignition_watch.h"
#include "button_sniffer.h"
#include "can_handler.h"
#include "haldex_modes.h"

static bool g_boot_done  = false;
static bool g_kl15_known = false;
static bool g_kl15_last  = false;
static bool g_bus_asleep = false;

void ignition_watch_init() {
  g_boot_done  = false;
  g_kl15_known = false;
  g_bus_asleep = false;
}

void ignition_watch_tick(uint32_t now) {
  if (!g_boot_done) {
    g_boot_done = true;
    haldex_modes_force_stock("demarrage BOT32");
  }

  // Cluster bus asleep / awake. Silence is measured between the last frame and
  // the last can_poll() (not the current clock), so a long stall elsewhere in
  // loop() — e.g. a blocked HTTP request — never looks like a sleeping bus.
  // The bus is only considered asleep if the ignition was last seen OFF (or is
  // unknown): a running car always reports Kl15 OFF before its bus sleeps, so a
  // false "wake" can never force STOCK while driving (start-stop included).
  CanStats cs = can_get_stats(CAN_CLUSTER);
  uint32_t silent;
  if (cs.last_rx_ms == 0) {
    silent = UINT32_MAX;                 // nothing received since boot = asleep
  } else {
    int32_t d = (int32_t)(can_last_poll_ms() - cs.last_rx_ms);
    silent = d < 0 ? 0 : (uint32_t)d;
  }
  bool ign_on_known = g_kl15_known && g_kl15_last;
  if (!g_bus_asleep && silent >= IGN_BUS_ASLEEP_MS && !ign_on_known) {
    g_bus_asleep = true;
    g_kl15_known = false;                // re-learn the ignition state on wake
  } else if (g_bus_asleep && silent < IGN_BUS_ASLEEP_MS) {
    g_bus_asleep = false;
    haldex_modes_force_stock("reveil du bus CAN");
  }

  // Ignition changes (fresh Klemmen_Status_01 frames only).
  if (button_sniffer_ignition_age_ms() > IGN_FRESH_MS) return;
  bool on = button_sniffer_ignition_on();
  if (!g_kl15_known) {
    g_kl15_known = true;
    g_kl15_last  = on;
    return;
  }
  if (on != g_kl15_last) {
    g_kl15_last = on;
    haldex_modes_force_stock(on ? "contact mis" : "contact coupe");
  }
}
