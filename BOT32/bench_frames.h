/*
 * bench_frames.h — bench "lamp killer" live frame injector.
 *
 * A table of candidate "healthy-bus" status frames (CAN ID + period + steady
 * payload + optional MQB CRC) derived from a real OFF->start capture. Each
 * frame can be toggled ON/OFF LIVE from the UI (no reflash) so the user can
 * map which frame extinguishes which cluster warning lamp on the bench.
 *
 * Safety: all frames default DISABLED at boot (nothing emitted). Frames are
 * only transmitted while bench mode is active (bench_frames_tick is called
 * from bench_test_tick) and tx_enabled is true.
 *
 * The frame table + CRC magic tables are GENERATED — see bench_frames.cpp and
 * tools/generate_bench_frames_cpp.py.
 */
#ifndef BOT32_BENCH_FRAMES_H
#define BOT32_BENCH_FRAMES_H

#include <Arduino.h>

void     bench_frames_init();
void     bench_frames_tick(uint32_t now);   // call from bench_test_tick()

uint8_t  bench_frames_count();              // number of candidate frames
uint16_t bench_frames_id(uint8_t idx);      // CAN ID of frame idx
uint16_t bench_frames_period(uint8_t idx);  // TX period (ms)
bool     bench_frames_needs_crc(uint8_t idx);
const char* bench_frames_group(uint8_t idx);// UI group label
const char* bench_frames_label(uint8_t idx);// human description (identified telltales)
bool     bench_frames_enabled(uint8_t idx); // current live enable state

bool     bench_frames_set_enabled(uint8_t idx, bool on);  // live toggle
void     bench_frames_all_off();            // disable everything (safety)

#endif // BOT32_BENCH_FRAMES_H
