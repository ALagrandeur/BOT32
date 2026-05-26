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

  // v2.1: optional extra UDS polls (off by default to keep MAP at full rate)
  bool     poll_ethanol;             // DID 0xF452 from engine ECU
  bool     poll_haldex_blockage;     // DID 0x2BF3 from Haldex ECU (0x70F/0x779)

  // v2.2: configurable cluster display override
  //   When a trigger CAN signal goes into "pressed" state, BOT32 starts
  //   transmitting a modified WBA_03 at 40Hz to show a custom value (e.g.
  //   ethanol %) in place of the gear indicator.
  bool     cluster_override_enabled;          // master toggle, default false
  uint16_t display_trigger_can_id;            // default 0x0FD (TC button)
  uint8_t  display_trigger_byte_idx;          // default 6 (byte index 0..7)
  uint8_t  display_trigger_rest_value;        // default 0x00 (TC enabled)
  uint8_t  display_trigger_pressed_value;     // default 0x03 (TC button held)
  uint8_t  display_value_source;              // 0=ethanol %, 1=haldex blockage %
  uint8_t  display_override_byte1_high;       // WBA_03 byte[1] high nibble (default 0x00 = blank, try other codes)
  // v2.3.0: encoding mode for byte[3] — lets user experiment with what the
  // cluster displays. The user's goal is showing the raw % (1%, 2%, ..., 100%)
  // without the P/R/N/D/S/M letters (which are confusing with real gears).
  uint8_t  display_byte3_value_mode;          // 0=raw full byte (default), 1=÷7 legacy, 2=tens digit, 3=units digit

  // Cluster TX rate
  uint16_t tx_rate_hz;       // default 30 Hz (v1.5.2+) (Motor_09)

  // Cluster CAN IDs (in case different cluster variant)
  uint16_t cluster_motor09_id;  // default 0x647
  uint16_t cluster_wba03_id;    // default 0x394

  // Behavior flags
  bool     tx_enabled;       // master switch: if false, NEVER TX on cluster (failsafe)
  bool     listen_only_boot; // if true, stay in listen-only for 5s at boot
  bool     force_tx_always;  // if true, TX Motor_09 in ALL modes (P/R/D too)
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

  // v2.2.1: bench-test support for cluster display override
  // - bench_display_value_pct: slider value (0..100) used as override source
  //   when bench mode is on (bypasses OBD2 poll which won't work on bench).
  // - bench_force_override: when true (AND bench_test_enabled), the trigger
  //   check in cluster_override is bypassed so you can see the cluster
  //   display the override value without simulating the trigger frame.
  uint8_t  bench_display_value_pct;  // 0..100
  bool     bench_force_override;     // bypass trigger detection (bench only)

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
bool settings_set_cluster_override_enabled(bool v);
bool settings_set_display_trigger_can_id(uint16_t v);
bool settings_set_display_trigger_byte_idx(uint8_t v);
bool settings_set_display_trigger_rest_value(uint8_t v);
bool settings_set_display_trigger_pressed_value(uint8_t v);
bool settings_set_display_value_source(uint8_t v);
bool settings_set_display_override_byte1_high(uint8_t v);
bool settings_set_display_byte3_value_mode(uint8_t v);
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
bool settings_set_bench_display_value_pct(uint8_t v);
bool settings_set_bench_force_override(bool v);
bool settings_set_haldex_enabled(bool v);
bool settings_set_haldex_bus(uint8_t v);
bool settings_set_haldex_state_id(uint16_t v);
bool settings_set_haldex_cmd_id(uint16_t v);
bool settings_set_haldex_transport(uint8_t v);
bool settings_set_haldex_espnow_peer_mac(const char* v);

// Reset to defaults (factory reset).
void settings_reset_to_defaults();

#endif // BOT32_SETTINGS_H
