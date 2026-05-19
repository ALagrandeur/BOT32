/*
 * Unified CAN handler — implementation.
 *
 * CAN_CLUSTER (channel 0) uses ESP32 TWAI internal controller.
 * CAN_OBD2    (channel 1) uses MCP2515 over SPI via ACAN2515 library.
 *
 * Required Arduino libraries:
 *   - none for TWAI (driver is in ESP32 core)
 *   - ACAN2515 by Pierre Molinaro
 *       Library Manager search: "ACAN2515"
 */
#include "can_handler.h"
#include "config.h"
#include "driver/twai.h"
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
//  Stats
// =============================================================
static CanStats stats[2] = { {0}, {0} };

// =============================================================
//  CAN_OBD2 — MCP2515 instance
// =============================================================
static ACAN2515 mcp2515(PIN_CAN1_CS, SPI, PIN_CAN1_INT);

// =============================================================
//  CAN_CLUSTER — TWAI init
// =============================================================
static bool init_twai_cluster() {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
    (gpio_num_t)PIN_CAN0_TX,
    (gpio_num_t)PIN_CAN0_RX,
    TWAI_MODE_NORMAL
  );
  // Increase TX queue size to absorb bursts
  g_config.tx_queue_len = 32;
  g_config.rx_queue_len = 32;

  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
    Serial.println("[CAN] TWAI driver install FAILED");
    return false;
  }
  if (twai_start() != ESP_OK) {
    Serial.println("[CAN] TWAI start FAILED");
    return false;
  }
  Serial.println("[CAN] TWAI started (cluster, 500 kbps)");
  return true;
}

// =============================================================
//  CAN_OBD2 — MCP2515 init
// =============================================================
static bool init_mcp2515_obd2() {
  SPI.begin(PIN_CAN1_SCK, PIN_CAN1_MISO, PIN_CAN1_MOSI);

  ACAN2515Settings settings(
    (uint32_t)MCP2515_CLOCK_MHZ * 1000000UL,
    500UL * 1000UL
  );
  // Use interrupt + polling fallback. ACAN2515 needs an ISR.
  settings.mRequestedMode = ACAN2515Settings::NormalMode;

  const uint16_t errorCode = mcp2515.begin(settings, [] { mcp2515.isr(); });
  if (errorCode != 0) {
    Serial.print("[CAN] MCP2515 begin FAILED, error 0x");
    Serial.println(errorCode, HEX);
    return false;
  }
  Serial.println("[CAN] MCP2515 started (OBD2, 500 kbps, 8 MHz xtal)");
  return true;
}

// =============================================================
//  Public lifecycle
// =============================================================
bool can_init() {
  bool ok = true;
  ok &= init_twai_cluster();
  ok &= init_mcp2515_obd2();
  return ok;
}

void can_poll() {
  // 1. Drain TWAI RX queue
  twai_message_t twai_msg;
  while (twai_receive(&twai_msg, 0) == ESP_OK) {
    CanFrame f;
    f.id  = twai_msg.identifier;
    f.len = twai_msg.data_length_code;
    f.timestamp = millis();
    memcpy(f.data, twai_msg.data, f.len);
    stats[CAN_CLUSTER].rx_count++;
    stats[CAN_CLUSTER].last_rx_ms = f.timestamp;
    for (uint8_t i = 0; i < n_listeners[CAN_CLUSTER]; i++) {
      listeners[CAN_CLUSTER][i](CAN_CLUSTER, f);
    }
  }

  // 2. Drain MCP2515 RX queue
  CANMessage mcp_msg;
  while (mcp2515.available()) {
    mcp2515.receive(mcp_msg);
    CanFrame f;
    f.id  = mcp_msg.id;
    f.len = mcp_msg.len;
    f.timestamp = millis();
    memcpy(f.data, mcp_msg.data, f.len);
    stats[CAN_OBD2].rx_count++;
    stats[CAN_OBD2].last_rx_ms = f.timestamp;
    for (uint8_t i = 0; i < n_listeners[CAN_OBD2]; i++) {
      listeners[CAN_OBD2][i](CAN_OBD2, f);
    }
  }

  // 3. Check TWAI for bus errors (poll alerts)
  twai_status_info_t status;
  if (twai_get_status_info(&status) == ESP_OK) {
    stats[CAN_CLUSTER].bus_errors = status.bus_error_count;
    if (status.state == TWAI_STATE_BUS_OFF) {
      Serial.println("[CAN] TWAI bus-off detected, initiating recovery");
      twai_initiate_recovery();
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

  if (ch == CAN_CLUSTER) {
    twai_message_t msg;
    msg.identifier = frame.id;
    msg.data_length_code = frame.len;
    msg.flags = 0;  // standard frame, not RTR
    memcpy(msg.data, frame.data, frame.len);
    esp_err_t r = twai_transmit(&msg, pdMS_TO_TICKS(10));
    if (r == ESP_OK) {
      stats[CAN_CLUSTER].tx_ok++;
      stats[CAN_CLUSTER].last_tx_ms = millis();
      return true;
    } else {
      stats[CAN_CLUSTER].tx_fail++;
      return false;
    }
  }

  if (ch == CAN_OBD2) {
    CANMessage msg;
    msg.id = frame.id;
    msg.len = frame.len;
    msg.ext = false;
    msg.rtr = false;
    memcpy(msg.data, frame.data, frame.len);
    bool ok = mcp2515.tryToSend(msg);
    if (ok) {
      stats[CAN_OBD2].tx_ok++;
      stats[CAN_OBD2].last_tx_ms = millis();
      return true;
    } else {
      stats[CAN_OBD2].tx_fail++;
      return false;
    }
  }

  return false;
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
  if (ch == CAN_CLUSTER) {
    twai_initiate_recovery();
    Serial.println("[CAN] TWAI recovery initiated");
    return true;
  }
  if (ch == CAN_OBD2) {
    // MCP2515 has no specific bus-off recovery API in ACAN2515 — re-init
    mcp2515.end();
    delay(50);
    return init_mcp2515_obd2();
  }
  return false;
}
