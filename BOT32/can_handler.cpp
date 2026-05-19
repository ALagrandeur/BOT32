/*
 * Unified CAN handler — implementation.
 *
 * Both CAN_CLUSTER (channel 0) and CAN_OBD2 (channel 1) use MCP2515 chips
 * on a SHARED SPI bus. Each has its own CS (chip select) and INT (interrupt)
 * pin. The ACAN2515 library handles SPI transactions safely.
 *
 * This setup matches the WaveShare 2-CH CAN HAT, which exposes 2 separate
 * MCP2515 controllers on one SPI bus, both with 3.3V SIT65HVD230 transceivers.
 *
 * Required Arduino libraries (install via Library Manager):
 *   - ACAN2515 by Pierre Molinaro (>= 2.1.x)
 */
#include "can_handler.h"
#include "config.h"
#include <ACAN2515.h>
#include <SPI.h>

// Safety blocklist (Airbag_01, Airbag_02) - NEVER transmit
static const uint32_t FORBIDDEN_IDS[] = { 0x040, 0x572 };
static const int N_FORBIDDEN = sizeof(FORBIDDEN_IDS) / sizeof(FORBIDDEN_IDS[0]);

// =============================================================
//  Listener registry (up to 4 per channel)
// =============================================================
#define MAX_LISTENERS 4
static CanRxCallback listeners[2][MAX_LISTENERS] = { { nullptr } };
static uint8_t        n_listeners[2] = { 0, 0 };

// =============================================================
//  Stats per channel
// =============================================================
static CanStats stats[2] = { {0}, {0} };

// =============================================================
//  Two MCP2515 instances on the SAME SPI bus.
//  Each has its own CS + INT pin. ACAN2515 uses SPI.beginTransaction()
//  internally so two instances share the bus safely.
// =============================================================
static ACAN2515 mcp_cluster(PIN_CAN0_CS, SPI, PIN_CAN0_INT);
static ACAN2515 mcp_obd2   (PIN_CAN1_CS, SPI, PIN_CAN1_INT);

// =============================================================
//  Init helpers
// =============================================================
static bool init_mcp(ACAN2515& chip, const char* name) {
  ACAN2515Settings settings(
    (uint32_t)MCP2515_CLOCK_MHZ * 1000000UL,
    500UL * 1000UL
  );
  settings.mRequestedMode = ACAN2515Settings::NormalMode;

  // Each chip needs its own ISR lambda capturing the right instance.
  // We pass the lambda inline; it's stored in a static slot by ACAN2515.
  uint16_t errorCode;
  if (&chip == &mcp_cluster) {
    errorCode = chip.begin(settings, [] { mcp_cluster.isr(); });
  } else {
    errorCode = chip.begin(settings, [] { mcp_obd2.isr(); });
  }

  if (errorCode != 0) {
    Serial.print("[CAN] ");
    Serial.print(name);
    Serial.print(" begin FAILED, error 0x");
    Serial.println(errorCode, HEX);
    return false;
  }
  Serial.print("[CAN] ");
  Serial.print(name);
  Serial.print(" MCP2515 started at 500 kbps (");
  Serial.print(MCP2515_CLOCK_MHZ);
  Serial.println(" MHz xtal)");
  return true;
}

// =============================================================
//  Public lifecycle
// =============================================================
bool can_init() {
  // Init shared SPI bus
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

  bool ok = true;
  ok &= init_mcp(mcp_cluster, "cluster");
  ok &= init_mcp(mcp_obd2,    "obd2");
  return ok;
}

void can_poll() {
  // Drain MCP2515 #0 RX queue (cluster)
  CANMessage rx;
  while (mcp_cluster.available()) {
    mcp_cluster.receive(rx);
    CanFrame f;
    f.id  = rx.id;
    f.len = rx.len;
    f.timestamp = millis();
    memcpy(f.data, rx.data, f.len);
    stats[CAN_CLUSTER].rx_count++;
    stats[CAN_CLUSTER].last_rx_ms = f.timestamp;
    for (uint8_t i = 0; i < n_listeners[CAN_CLUSTER]; i++) {
      listeners[CAN_CLUSTER][i](CAN_CLUSTER, f);
    }
  }

  // Drain MCP2515 #1 RX queue (OBD2)
  while (mcp_obd2.available()) {
    mcp_obd2.receive(rx);
    CanFrame f;
    f.id  = rx.id;
    f.len = rx.len;
    f.timestamp = millis();
    memcpy(f.data, rx.data, f.len);
    stats[CAN_OBD2].rx_count++;
    stats[CAN_OBD2].last_rx_ms = f.timestamp;
    for (uint8_t i = 0; i < n_listeners[CAN_OBD2]; i++) {
      listeners[CAN_OBD2][i](CAN_OBD2, f);
    }
  }
}

// =============================================================
//  TX
// =============================================================
bool can_send(CanChannel ch, const CanFrame& frame) {
  // Safety: airbag blocklist
  for (int i = 0; i < N_FORBIDDEN; i++) {
    if (frame.id == FORBIDDEN_IDS[i]) {
      Serial.print("[CAN] BLOCKED forbidden TX id 0x");
      Serial.println(frame.id, HEX);
      return false;
    }
  }

  CANMessage msg;
  msg.id  = frame.id;
  msg.len = frame.len;
  msg.ext = false;
  msg.rtr = false;
  memcpy(msg.data, frame.data, frame.len);

  bool ok = false;
  if (ch == CAN_CLUSTER) {
    ok = mcp_cluster.tryToSend(msg);
  } else if (ch == CAN_OBD2) {
    ok = mcp_obd2.tryToSend(msg);
  } else {
    return false;
  }

  if (ok) {
    stats[ch].tx_ok++;
    stats[ch].last_tx_ms = millis();
  } else {
    stats[ch].tx_fail++;
  }
  return ok;
}

// =============================================================
//  Listener registration
// =============================================================
void can_register_listener(CanChannel ch, CanRxCallback cb) {
  if (ch >= 2 || cb == nullptr) return;
  if (n_listeners[ch] >= MAX_LISTENERS) return;
  listeners[ch][n_listeners[ch]++] = cb;
}

// =============================================================
//  Diagnostics
// =============================================================
CanStats can_get_stats(CanChannel ch) {
  if (ch >= 2) return {0};
  return stats[ch];
}

bool can_recover(CanChannel ch) {
  // MCP2515 recovery: re-init the chip from scratch
  if (ch == CAN_CLUSTER) {
    mcp_cluster.end();
    delay(50);
    return init_mcp(mcp_cluster, "cluster (recovery)");
  }
  if (ch == CAN_OBD2) {
    mcp_obd2.end();
    delay(50);
    return init_mcp(mcp_obd2, "obd2 (recovery)");
  }
  return false;
}
