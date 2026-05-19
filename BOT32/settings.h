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
  // MAP -> coolant byte mapping
  float    map_min_mbar;     // MAP at idle -> displays as low needle (~50 C)
  float    map_max_mbar;     // MAP at full boost -> displays as high needle (~130 C)
  float    scale;            // fine adjustment (1.0 = no change)
  float    offset_c;         // fine adjustment in C (0 = no change)

  // OBD2 polling
  uint16_t obd2_req_id;      // default 0x7E0
  uint16_t obd2_resp_id;     // default 0x7E8
  uint16_t obd2_did_map;     // default 0x39C0 (Saugrohrdruck)
  uint16_t obd2_poll_hz;     // default 5

  // Cluster TX rate
  uint16_t tx_rate_hz;       // default 20 (Motor_09)

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
bool settings_set_scale(float v);
bool settings_set_offset_c(float v);
bool settings_set_obd2_did_map(uint16_t v);
bool settings_set_obd2_poll_hz(uint16_t v);
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

// Reset to defaults (factory reset).
void settings_reset_to_defaults();

#endif // BOT32_SETTINGS_H
