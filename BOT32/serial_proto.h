/*
 * Serial protocol over USB — line-delimited JSON, ESP32 ↔ PC Python webui.
 *
 * Why JSON? Easy to debug (just open Serial Monitor), trivial to parse on PC,
 * extensible. Performance is fine at 115200 baud — even at 50 frames/sec total
 * across both CAN buses, we send ~5 KB/sec which is well under the link cap.
 *
 * One JSON object per line, terminated by '\n'.
 *
 * ──────────────────────────────────────────────────────────────────
 * PC → ESP32 commands (request):
 * ──────────────────────────────────────────────────────────────────
 *   {"cmd": "ping"}
 *   {"cmd": "get_status"}
 *   {"cmd": "get_settings"}
 *   {"cmd": "set", "key": "map_max_mbar", "value": 2200}
 *   {"cmd": "set", "key": "tx_enabled", "value": false}
 *   {"cmd": "factory_reset"}
 *   {"cmd": "subscribe_frames", "enabled": true}
 *
 * ──────────────────────────────────────────────────────────────────
 * ESP32 → PC notifications (event):
 * ──────────────────────────────────────────────────────────────────
 *   {"evt": "boot", "version": "0.1", "build": "2026-05-18"}
 *   {"evt": "status", "mode": "BOOST", "lever": "S3", "map_mbar": 1450,
 *                     "coolant_byte": 234, "tx_count_cluster": 4500, ...}
 *   {"evt": "settings", ... full settings object ... }
 *   {"evt": "frame", "bus": "cluster", "dir": "rx", "id": 916, "len": 8,
 *                    "data": [0,64,0,4,0,0,0,0], "ts_ms": 12345}
 *   {"evt": "log", "level": "info", "msg": "TWAI started"}
 *   {"evt": "pong"}
 *   {"evt": "ack", "for": "set", "ok": true}
 *
 * The TX frames (Motor_09 we send) are also reported with dir="tx" so the PC
 * sees both directions.
 */
#ifndef BOT32_SERIAL_PROTO_H
#define BOT32_SERIAL_PROTO_H

#include <Arduino.h>
#include "can_handler.h"

// Initialize the serial protocol module (registers CAN RX listeners for
// frame mirroring). Call after Serial.begin() and after can_init().
void serial_proto_init();

// Process incoming serial commands. Call from loop().
void serial_proto_poll();

// Periodic status broadcast (status JSON every 500ms to PC if connected).
// Call from loop().
void serial_proto_tick();

// Manually report a TX frame to the PC (since CAN listeners only see RX).
// Call this after every successful can_send().
void serial_proto_report_tx(CanChannel ch, const CanFrame& f);

// Emit a log line to the PC.
void serial_proto_log(const char* level, const char* msg);

#endif // BOT32_SERIAL_PROTO_H
