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
//  MAP -> boost mapping defaults (v1.6.0 — user-calibrated on real vehicle)
//    Alltrack 2017 EA888:
//      MAP at idle (vacuum)    ~250 mbar      -> 50 C (cold needle)
//      MAP at full boost       ~2068 mbar     -> 130 C (red zone)
//    Default mapping = LINEAR (no dead-zone skip).
//    Optional dead-zone mode available via settings (use_dead_zone_mapping).
// =============================================================
#define MAP_MIN_MBAR_DEFAULT    250.0f   // -> displays as 50 C (cold needle)
#define MAP_MAX_MBAR_DEFAULT   2068.0f   // -> displays as 130 C (red zone, full boost)

// =============================================================
//  Rates
// =============================================================
#define OBD2_POLL_INTERVAL_MS   200    // 5 Hz UDS query for MAP
#define MOTOR_09_TX_INTERVAL_MS 33     // 30 Hz override (v1.5.2 — was 20Hz, smoother needle)
#define MAP_STALE_TIMEOUT_MS    1500   // if no MAP for 1.5s -> fall back to min

// =============================================================
//  Pins — Hardware: WaveShare 2-CH CAN HAT (2x MCP2515 + 2x SIT65HVD230)
//  wired to ESP32 via Dupont jumpers on shared SPI bus.
//  See docs/wiring_waveshare_hat.md for the schema.
//
//    CAN0 = MCP2515 #0 (CS0) -> SIT65HVD230 #0 (3.3V) -> CLUSTER bus
//    CAN1 = MCP2515 #1 (CS1) -> SIT65HVD230 #1 (3.3V) -> OBD2 bus
//
//  Both buses use the SAME SPI bus (SCK/MISO/MOSI shared), distinguished
//  by separate CS (chip select) and INT pins. No level shifter needed —
//  HAT runs entirely at 3.3V via the VIO jumper position "3V3".
// =============================================================
#define PIN_LED_STATUS    2     // built-in LED on most ESP32 boards

// Shared SPI bus (VSPI on ESP32)
#define PIN_SPI_SCK      18     // SPI clock      -> HAT SCK
#define PIN_SPI_MISO     19     // SPI MISO       <- HAT MISO
#define PIN_SPI_MOSI     23     // SPI MOSI       -> HAT MOSI

// CAN0 = MCP2515 #0 = cluster bus
#define PIN_CAN0_CS       5     // -> HAT CS0
#define PIN_CAN0_INT      4     // <- HAT INT0

// CAN1 = MCP2515 #1 = OBD2 bus
#define PIN_CAN1_CS      25     // -> HAT CS1 (GPIO 25 chosen to avoid strapping pins)
#define PIN_CAN1_INT     26     // <- HAT INT1

// MCP2515 module clock frequency — CRITICAL: must match physical crystal !
//
// WaveShare 2-CH CAN HAT uses 16 MHz crystals (confirmed via WaveShare wiki +
// product page + community forums). If you have a DIFFERENT board, check the
// silver crystal cans next to each MCP2515 chip — markings will say "8.000",
// "12.000", "16.000", or "20.000".
//
// IF THIS VALUE IS WRONG:
//   - chip initialization succeeds (SPI works regardless)
//   - BUT all CAN bit timing is OFF by ratio (config/actual)
//   - example: setting 12 here when crystal is 16 -> bit rate becomes
//     500 * (16/12) = 666 kbps instead of 500 -> no frames ever go through
//   - symptom: "MCP2515 started" in Serial but rx=0, tx_fail high, no CAN
#define MCP2515_CLOCK_MHZ  16

#endif // BOT32_CONFIG_H
