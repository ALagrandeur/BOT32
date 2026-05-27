/*
 * wifi_ui.h — Optional WiFi AP + mini HTTP server for phone access (v2.6.0)
 *
 * When enabled in settings, BOT32 broadcasts a WiFi Access Point. A phone
 * connects to it and browses to http://192.168.4.1 to get a mobile-friendly
 * page showing live data + a few essential controls.
 *
 * USB serial path is completely unchanged — this is purely additive.
 *
 * Architecture:
 *   - WIFI_AP mode (or WIFI_AP_STA if Haldex ESP-NOW is also active)
 *   - WebServer on port 80 (built-in Arduino-ESP32 library, no extra deps)
 *   - Routes:
 *       GET  /            -> mobile HTML page (embedded as PROGMEM)
 *       GET  /api/status  -> JSON live status (same fields as serial proto)
 *       GET  /api/settings -> JSON full settings
 *       POST /api/cmd     -> execute a command (clear_engine_fault, set_haldex_mode, etc.)
 *       POST /api/set     -> change a setting (body = {key, value})
 *
 * Defaults: SSID "BOT32", password "BOT32-2026" (WPA2, configurable in UI).
 *
 * Memory footprint: ~3 KB for static HTML PROGMEM. WebServer + WiFi stack
 * ~25 KB RAM. Acceptable for ESP32-WROOM-32 with 320KB RAM.
 */
#ifndef BOT32_WIFI_UI_H
#define BOT32_WIFI_UI_H

#include <Arduino.h>

// Initialize the module (does NOT start AP — that happens in wifi_ui_apply()
// based on settings). Call once at boot.
void wifi_ui_init();

// Tick — call from loop(). Services HTTP requests when AP is active.
void wifi_ui_tick();

// Apply the current settings (start/stop AP, change SSID, etc.).
// Call after settings_init() and any time settings change.
void wifi_ui_apply();

// Diagnostic
bool   wifi_ui_is_active();
String wifi_ui_get_ip();        // ESP32 AP IP address (typically "192.168.4.1")
String wifi_ui_get_ssid();      // currently broadcasting SSID
uint8_t wifi_ui_get_n_clients();  // number of connected clients

#endif // BOT32_WIFI_UI_H
