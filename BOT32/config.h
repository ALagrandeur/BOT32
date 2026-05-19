/*
 * BOT32 — compile-time configuration.
 * All CAN IDs, pins, rates, magic constants here.
 */
#ifndef BOT32_CONFIG_H
#define BOT32_CONFIG_H

#include <Arduino.h>

// =============================================================
//  CAN IDs (VW MQB platform, 5G1 920 740B cluster validated)
// =============================================================

// Cluster bus (CAN0)
#define CAN_ID_MOTOR_09        0x647   // TX: coolant override (us) / sniff real
#define CAN_ID_WBA_03          0x394   // RX: gear lever (gateway -> cluster)

// OBD2 bus (CAN1)
#define CAN_ID_OBD2_REQ        0x7E0   // TX: UDS request to engine ECU
#define CAN_ID_OBD2_RESP       0x7E8   // RX: UDS response from engine ECU
#define UDS_DID_MAP            0x39C0  // ReadDataByIdentifier: Saugrohrdruck (MAP, mbar)

// =============================================================
//  Forbidden IDs (NEVER TX)
// =============================================================
#define CAN_ID_AIRBAG_01       0x040
#define CAN_ID_AIRBAG_02       0x572

// =============================================================
//  Motor_09 magic bytes (validated on real ECU + r00li/CarCluster)
// =============================================================
// Format: [byte0=coolant, 0xFD, 0xFF, 0x7F, 0x00, 0x00, 0x00, 0xC1]
// byte 7 cycles 0x13/0x18/0xC1/0x12 on real ECU; static 0xC1 accepted by cluster
extern const uint8_t MOTOR_09_TAIL[7];   // defined in vw_mqb.cpp

// =============================================================
//  Coolant byte <-> temp formula (linear)
//    temp_C = byte * 0.7339 - 43.94
//    byte   = (temp_C + 43.94) / 0.7339
//    0x80 (128) = 50.0 C    -> needle at cold (low boost)
//    0xED (237) = 130.0 C   -> needle at red zone (high boost)
// =============================================================
#define COOLANT_FORMULA_SCALE   0.7339f
#define COOLANT_FORMULA_OFFSET  -43.94f
#define COOLANT_TEMP_MIN_C      50.0f
#define COOLANT_TEMP_MAX_C      130.0f

// =============================================================
//  Cluster dead zone (gauge needle damping)
//    For any real coolant in [80, 110]C, cluster holds needle at center.
//    Mapping must SKIP this zone to ensure visible needle movement.
// =============================================================
#define CLUSTER_DEAD_ZONE_LOW_C   80.0f
#define CLUSTER_DEAD_ZONE_HIGH_C  110.0f
#define DEAD_ZONE_SAFE_MARGIN_C   1.0f

// =============================================================
//  MAP -> boost mapping defaults (calibrate empirically)
//    Alltrack 2017 EA888 typical:
//      MAP at idle  ~300-400 mbar
//      MAP at boost ~2000-2500 mbar
// =============================================================
#define MAP_MIN_MBAR_DEFAULT   300.0f   // -> displays as ~50 C (cold needle)
#define MAP_MAX_MBAR_DEFAULT   2500.0f  // -> displays as ~130 C (red zone)

// =============================================================
//  Rates
// =============================================================
#define OBD2_POLL_INTERVAL_MS   200    // 5 Hz UDS query for MAP
#define MOTOR_09_TX_INTERVAL_MS 50     // 20 Hz override (matches real ECU rate)
#define MAP_STALE_TIMEOUT_MS    1500   // if no MAP for 1.5s -> fall back to min

// =============================================================
//  Pins — Option A hardware (see docs/wiring.md)
//    CAN0 = TWAI internal + SN65HVD230 (3.3V) -> CLUSTER bus
//    CAN1 = MCP2515 SPI + TJA1050 (5V) + level shifter -> OBD2 bus
// =============================================================
#define PIN_LED_STATUS    2     // built-in LED on most ESP32 boards

// CAN0 (TWAI internal -> cluster)
#define PIN_CAN0_TX      21     // ESP32 GPIO 21 -> SN65HVD230 CTX
#define PIN_CAN0_RX      22     // ESP32 GPIO 22 <- SN65HVD230 CRX

// CAN1 (MCP2515 over SPI -> OBD2)
// IMPORTANT: MISO + INT lines need 3.3V/5V level shifter (TXS0108E or BSS138)
#define PIN_CAN1_CS       5     // SPI chip select (via shifter)
#define PIN_CAN1_INT      4     // MCP2515 IRQ (via shifter, MCP2515 -> ESP32)
#define PIN_CAN1_SCK     18     // SPI clock (via shifter)
#define PIN_CAN1_MISO    19     // SPI MISO (via shifter, MCP2515 -> ESP32)
#define PIN_CAN1_MOSI    23     // SPI MOSI (via shifter)

// MCP2515 module clock frequency (most modules: 8 MHz; some: 16 MHz)
// Common Chinese MCP2515 modules use 8 MHz crystal.
#define MCP2515_CLOCK_MHZ  8

#endif // BOT32_CONFIG_H
