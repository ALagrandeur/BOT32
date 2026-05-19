/*
 * Bench test mode — standalone cluster test, NO vehicle required.
 *
 * When settings.bench_test_enabled = true, BOT32 emits the full set of frames
 * that a standalone cluster needs to wake up and accept our signals. This
 * mirrors what mk7-cluster-bench-controller (sister project) does on the bench.
 *
 * Frames emitted (on the bus selected by settings.bench_test_bus):
 *
 *   ID      Name             Rate    CRC?  Purpose
 *   ──────────────────────────────────────────────────────────────────────────
 *   0x3C0   Klemmen_Status_01 10 Hz  yes   Wake up cluster (Kl.15 simulated)
 *   0x641   Motor_Code_01    20 Hz   yes   Engine ECU "alive" heartbeat
 *   0x107   Motor_04 (RPM)   20 Hz   no    Drives RPM tachometer needle
 *   0x647   Motor_09 (cool)  20 Hz   no    Drives coolant gauge needle (boost)
 *   0x040   Airbag_01        10 Hz   yes   ⚠ Only if block_airbag = OFF
 *   0x106   ESP_05           10 Hz   no    ESP heartbeat
 *   0x116   ESP_10           10 Hz   yes   ESP heartbeat
 *   0x65D   ESP_20           10 Hz   no    ESP heartbeat
 *   0x31E   TSK_07           10 Hz   yes   Torque controller heartbeat
 *   0x32A   LH_EPS_01        10 Hz   yes   Power steering heartbeat
 *
 * RPM and MAP values come from settings.bench_rpm + settings.bench_map_mbar
 * (driven by the UI sliders).
 *
 * Without this bundle, a standalone cluster (powered but no real gateway) will
 * not display the boost gauge correctly because it expects "alive ECUs"
 * heartbeats on the bus.
 */
#ifndef BOT32_BENCH_TEST_H
#define BOT32_BENCH_TEST_H

#include <Arduino.h>

// Initialize bench test module. Call once at boot.
void bench_test_init();

// Main tick — call from loop(). Returns true if bench mode is currently active
// (in which case the caller should SKIP normal vehicle TX logic to avoid
// duplicate Motor_09 frames colliding).
bool bench_test_tick();

#endif // BOT32_BENCH_TEST_H
