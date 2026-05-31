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

// OBD2 bus (CAN1) — UDS / OBD-II diagnostic addressing
// Standard request->response pair: 0x7E0->0x7E8 (Engine), 0x7E1->0x7E9 (Trans),
// VW extended addresses: 0x713->0x77D (Gateway), 0x70F->0x779 (Haldex), etc.
// Functional broadcast: 0x700 (OBD-II), 0x7DF (UDS)
#define CAN_ID_OBD2_REQ        0x7E0   // TX: UDS request to engine ECU
#define CAN_ID_OBD2_RESP       0x7E8   // RX: UDS response from engine ECU
#define CAN_ID_OBD2_BROADCAST  0x700   // TX: OBD-II functional broadcast (Mode 04 clear DTC)
#define CAN_ID_HALDEX_REQ      0x70F   // TX: UDS request to Haldex ECU (vehicle-confirmed v2.1)
#define CAN_ID_HALDEX_RESP     0x779   // RX: UDS response from Haldex ECU
#define CAN_ID_TCM_REQ         0x7E1   // TX: UDS request to Transmission ECU (DSG) — v2.8.0
#define CAN_ID_TCM_RESP        0x7E9   // RX: UDS response from Transmission ECU (DSG) — v2.8.0

// Cluster-bus sniffer IDs — v2.8.0+
// Hand brake state appears in KOMBI_01 (0x30B) byte[2] bit 7
//   0x80 = engaged (ON), 0x00 = released (OFF)
// MFSW (multi-function steering wheel) OK button appears in 0x5BF byte[0]
//   0x07 = released, 0x00 = pressed
#define CAN_ID_KOMBI_01        0x30B   // RX: cluster status (hand brake + others)
#define CAN_ID_MFSW            0x5BF   // RX: multi-function steering-wheel buttons

// v2.9.0 — two new button sniffers (passive)
// Hazard button — Blinkmodi_01 (0x366) byte[2] bit 4 (0x10) — vehicle-confirmed
//   0x10 = Hazard ON, 0x00 = Hazard OFF
// Traction Control button — ESP_21 (0x0FD) byte[6] — vehicle-confirmed
//   0x03 = TC button held (pressed), 0x00 = TC enabled (released)
#define CAN_ID_HAZARD          0x366   // RX: Blinkmodi_01 (hazard switch)
#define CAN_ID_TC_BUTTON       0x0FD   // RX: ESP_21 (Traction Control button)

// UDS Data Identifiers (DIDs) — all confirmed on MK7 Alltrack 2017
#define UDS_DID_MAP                0x39C0  // Saugrohrdruck (MAP, mbar, 16-bit) — engine ECU
#define UDS_DID_ETHANOL            0xF452  // Ethanol content (8-bit, raw*100/255 = %) — engine ECU
#define UDS_DID_HALDEX_BLOCKAGE    0x2BF3  // Haldex degree of blockage (16-bit) — Haldex ECU
// v2.8.0 — three temperature DIDs (validated 2026-05-26 via SavvyCAN captures)
#define UDS_DID_DSG_OIL            0x2104  // DSG transmission oil temp — TCM (0x7E1/0x7E9)
                                           //   formula: temp_C = data[4]
#define UDS_DID_EGT                0x40D5  // Exhaust gas temp (bank 1, sensor 1) — engine ECU
                                           //   formula: temp_C = ((data[4]<<8)|data[5]) - 250
#define UDS_DID_ENGINE_OIL         0xF43C  // Engine oil temperature — engine ECU
                                           //   formula: temp_C = data[5] - 8
#define UDS_OBD2_MODE_CLEAR_DTC    0x04    // OBD-II Mode 04 (clear emissions DTCs, broadcast)

// Stale timeouts for cached UDS read values
#define ETHANOL_STALE_TIMEOUT_MS         5000
#define HALDEX_BLOCKAGE_STALE_TIMEOUT_MS 2000
// v2.8.0 — temp values change slowly, keep generous timeouts
#define DSG_OIL_STALE_TIMEOUT_MS         5000
#define EGT_STALE_TIMEOUT_MS             5000
#define ENGINE_OIL_STALE_TIMEOUT_MS      5000
// v2.8.0 — sniffer staleness (cluster bus broadcasts these IDs at ~10-20 Hz)
#define HANDBRAKE_STALE_TIMEOUT_MS       2000
#define MFSW_STALE_TIMEOUT_MS            2000

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
//  MAP -> boost mapping defaults (v2.0 — vehicle-validated on Alltrack 2017 EA888)
//      MAP at idle (vacuum)    ~250 mbar      -> 50 C (cold needle)
//      MAP at full boost       ~2068 mbar     -> 130 C (red zone)
//    Mapping = LINEAR (vehicle test confirmed; dead-zone-aware mode removed).
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
