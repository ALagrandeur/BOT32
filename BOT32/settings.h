/*
 * Settings persistence — stores user-tunable params in ESP32 NVS (flash).
 *
 * Values survive reboot. Modified via USB serial (web UI on PC).
 * Defaults defined here; load at boot, save on change.
 */
#ifndef BOT32_SETTINGS_H
#define BOT32_SETTINGS_H

#include <Arduino.h>

struct Settings {
  // MAP -> coolant byte mapping (linear, v2.0)
  float    map_min_mbar;     // MAP at idle -> displays as 50 C (cold needle)
  float    map_max_mbar;     // MAP at full boost -> displays as 130 C (red zone)

  // OBD2 polling
  uint16_t obd2_req_id;      // default 0x7E0
  uint16_t obd2_resp_id;     // default 0x7E8
  uint16_t obd2_did_map;     // default 0x39C0 (Saugrohrdruck)
  uint16_t obd2_poll_hz;     // default 5 (per slot at this rate; round-robin if multiple polls enabled)

  // v2.1: extra UDS polls (v2.4.0: now ON by default, no UI toggle)
  bool     poll_ethanol;             // DID 0xF452 from engine ECU
  bool     poll_haldex_blockage;     // DID 0x2BF3 from Haldex ECU (0x70F/0x779)

  // v2.9.0: cluster display override removed entirely.
  //   (Was v2.2 — see git history v2.8.0 and earlier for the original feature.)

  // v2.4.0+: trigger config for AUTOMATIC Clear Engine Fault when a physical
  // vehicle button is pressed. Fields are persisted now; the auto-trigger
  // detector in firmware is ROADMAP (not yet implemented).
  // v2.5.1 defaults: Hazard button, 3 ON/OFF cycles within 4 seconds.
  bool     cef_auto_enabled;          // master toggle for auto-trigger (default true)
  uint16_t cef_trigger_can_id;        // default 0x366 (Hazard / Blinkmodi_01)
  uint8_t  cef_trigger_byte_idx;      // default 2 (D3)
  uint8_t  cef_trigger_rest_value;    // default 0x00 (Hazard OFF)
  uint8_t  cef_trigger_pressed_value; // default 0x10 (Hazard ON bit)
  uint8_t  cef_press_count;           // number of ON/OFF cycles required (default 3)
  uint16_t cef_press_window_ms;       // time window for the press sequence (default 4000 ms)

  // v2.6.0: WiFi AP mode for phone access while in vehicle.
  // USB serial path remains 100% active in parallel — this is purely additive.
  // When wifi_enabled = true, ESP32 starts a WiFi AP and serves a mobile
  // HTML page at http://192.168.4.1 (default IP for ESP32 AP mode).
  bool     wifi_enabled;              // master toggle (default false)
  char     wifi_ap_ssid[33];          // AP SSID (default "BOT32")
  char     wifi_ap_password[64];      // AP password (default "BOT32-2026", WPA2, min 8 chars)

  // Cluster TX rate
  uint16_t tx_rate_hz;       // default 30 Hz (v1.5.2+) (Motor_09)

  // Cluster CAN IDs (in case different cluster variant)
  uint16_t cluster_motor09_id;  // default 0x647
  uint16_t cluster_wba03_id;    // default 0x394

  // Behavior flags
  bool     tx_enabled;       // master switch: if false, NEVER TX on cluster (failsafe)
  bool     listen_only_boot; // if true, stay in listen-only for 5s at boot
  bool     force_tx_always;  // if true, TX Motor_09 in ALL modes (P/R/N/D too)
                             // — useful for bench diagnostic; default false
  bool     block_airbag;     // if true, block all TX on airbag IDs (0x040, 0x572)
                             // — default true (SAFETY); only disable knowingly

  // Bench test mode — standalone cluster test, no vehicle.
  // When enabled, BOT32 emits the full sister-project bundle (Wake, Motor_Code_01,
  // RPM, Motor_09, ESP/TSK/LH_EPS heartbeats) so a standalone cluster on the bench
  // accepts our signals. Normal vehicle logic is suspended while bench is ON.
  bool     bench_test_enabled;   // master toggle, default false
  uint16_t bench_rpm;            // 0..8000 RPM (Motor_04 bytes 3-4)
  uint16_t bench_map_mbar;       // 0..3000 mbar (Motor_09 byte 0 via coolant mapping)
  uint8_t  bench_test_bus;       // 0 = TX on CAN_CLUSTER, 1 = TX on CAN_OBD2

  // v2.9.0: bench_display_value_pct + bench_force_override removed
  // (they were only used by the cluster display override feature, now deleted).

  // v2.7.1: bench mode auto-toggles tx_enabled (forces ON on enter, restores
  // previous on exit). This field memorizes the user's tx_enabled state at
  // the moment bench was activated, so we can restore it on deactivate.
  bool     tx_enabled_before_bench;

  // Haldex link — talks to an external Haldex MITM device.
  // BOT32 acts as a client (reads state broadcasts, sends mode commands).
  // The actual Haldex bus MITM runs on separate hardware (e.g., OpenHaldex-C6
  // OR a user-built ESP32 + 2 CAN modules setup).
  bool     haldex_enabled;       // master toggle, default false
  uint8_t  haldex_bus;           // 0 = CAN_CLUSTER, 1 = CAN_OBD2 (used if haldex_transport=0)
  uint16_t haldex_state_id;      // state broadcast CAN ID (used if transport=0)
  uint16_t haldex_cmd_id;        // mode command CAN ID (used if transport=0)

  // Transport selection: 0 = CAN (both devices share a CAN bus, OpenHaldex
  // style), 1 = ESP-NOW (wireless peer-to-peer between BOT32 and the MITM
  // ESP32, useful when the MITM has no chassis CAN connection).
  uint8_t  haldex_transport;     // 0 = CAN, 1 = ESP-NOW (default 0)
  // Peer MAC for ESP-NOW. Format "AA:BB:CC:DD:EE:FF". Empty string => use
  // broadcast (FF:FF:FF:FF:FF:FF). Recommended: set to the MITM ESP32 MAC
  // for production to avoid receiving from other ESP-NOW devices nearby.
  char     haldex_espnow_peer_mac[18];

  uint8_t  version;              // settings struct version (for migration)
};

// Initialize NVS, load settings (or defaults if none).
void settings_init();

// Get a reference to the current settings (read-only).
const Settings& settings_get();

// Set + persist a value. Returns true if saved.
// Use these from serial_proto when a config update arrives from PC.
bool settings_set_map_min_mbar(float v);
bool settings_set_map_max_mbar(float v);
bool settings_set_obd2_did_map(uint16_t v);
bool settings_set_obd2_poll_hz(uint16_t v);
bool settings_set_poll_ethanol(bool v);
bool settings_set_poll_haldex_blockage(bool v);
// v2.9.0: 7 cluster_override setters removed (feature deleted).
bool settings_set_cef_auto_enabled(bool v);
bool settings_set_cef_trigger_can_id(uint16_t v);
bool settings_set_cef_trigger_byte_idx(uint8_t v);
bool settings_set_cef_trigger_rest_value(uint8_t v);
bool settings_set_cef_trigger_pressed_value(uint8_t v);
bool settings_set_cef_press_count(uint8_t v);
bool settings_set_cef_press_window_ms(uint16_t v);
bool settings_set_wifi_enabled(bool v);
bool settings_set_wifi_ap_ssid(const char* v);
bool settings_set_wifi_ap_password(const char* v);
bool settings_set_tx_rate_hz(uint16_t v);
bool settings_set_tx_enabled(bool v);
bool settings_set_force_tx_always(bool v);
bool settings_set_block_airbag(bool v);
bool settings_set_cluster_motor09_id(uint16_t v);
bool settings_set_cluster_wba03_id(uint16_t v);
bool settings_set_obd2_req_id(uint16_t v);
bool settings_set_obd2_resp_id(uint16_t v);
bool settings_set_bench_test_enabled(bool v);
bool settings_set_bench_rpm(uint16_t v);
bool settings_set_bench_map_mbar(uint16_t v);
bool settings_set_bench_test_bus(uint8_t v);
// v2.9.0: bench_display_value_pct + bench_force_override setters removed.
bool settings_set_haldex_enabled(bool v);
bool settings_set_haldex_bus(uint8_t v);
bool settings_set_haldex_state_id(uint16_t v);
bool settings_set_haldex_cmd_id(uint16_t v);
bool settings_set_haldex_transport(uint8_t v);
bool settings_set_haldex_espnow_peer_mac(const char* v);

// Reset to defaults (factory reset).
void settings_reset_to_defaults();

#endif // BOT32_SETTINGS_H
