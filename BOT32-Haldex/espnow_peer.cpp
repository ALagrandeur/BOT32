/*
 * espnow_peer.cpp — implementation
 *
 * Mirror of BOT32 main's haldex_espnow module, but on the MITM side:
 * - Listens for SET_MODE  (0x02) -> calls mode_state_set()
 * - Periodically sends STATE (0x01) with current bus state
 *
 * Protocol designed for BOT32 (fresh, not from any 3rd-party project).
 */
#include "espnow_peer.h"
#include "config.h"
#include "mode_state.h"
#include "haldex_mitm.h"
#include <WiFi.h>
#include <esp_now.h>

// =============================================================
//  Protocol constants
// =============================================================
static const uint8_t MAGIC_0 = 0xBA;
static const uint8_t MAGIC_1 = 0xB0;
#define PKT_TYPE_STATE     0x01
#define PKT_TYPE_SET_MODE  0x02
#define PKT_LEN_STATE      10
#define PKT_LEN_SET_MODE   4

// =============================================================
//  Peer config
// =============================================================
// MAC of BOT32 main. If you know it, fill in here. Default = broadcast
// so any BOT32 main with the right magic header will receive our STATE.
// Format: { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF }
// To find BOT32 main's MAC: open its web UI -> Haldex card -> ESP-NOW
// section -> "BOT32 MAC address" field.
static uint8_t BOT32_MAIN_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static bool g_initialized = false;
static uint32_t g_last_state_tx_ms = 0;

// =============================================================
//  ESP-NOW callbacks
// =============================================================
static void on_data_recv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len < 4) return;
  if (data[0] != MAGIC_0 || data[1] != MAGIC_1) return;

  uint8_t type = data[2];
  if (type == PKT_TYPE_SET_MODE && len >= PKT_LEN_SET_MODE) {
    uint8_t mode = data[3];
    mode_state_set(mode);
    Serial.print("[espnow] RX SET_MODE = ");
    Serial.println(mode);
  }
  // Other packet types could be added here in the future
}

static void on_data_sent(const uint8_t* mac, esp_now_send_status_t status) {
  (void)mac; (void)status;
  // could log TX failures if desired
}

// =============================================================
//  Public API
// =============================================================
void espnow_peer_init() {
  if (g_initialized) return;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("[espnow] init FAILED");
    return;
  }

  esp_now_register_recv_cb(on_data_recv);
  esp_now_register_send_cb(on_data_sent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BOT32_MAIN_MAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    if (!esp_now_is_peer_exist(BOT32_MAIN_MAC)) {
      Serial.println("[espnow] add_peer FAILED");
      return;
    }
  }

  g_initialized = true;
  Serial.print("[espnow] init OK, my MAC = ");
  Serial.println(WiFi.macAddress());
  Serial.println("[espnow] sending STATE to broadcast (configure BOT32_MAIN_MAC for direct addressing)");
}

void espnow_peer_send_state() {
  if (!g_initialized) return;

  // Build STATE packet from current values
  uint8_t pkt[PKT_LEN_STATE];
  pkt[0] = MAGIC_0;
  pkt[1] = MAGIC_1;
  pkt[2] = PKT_TYPE_STATE;
  pkt[3] = mode_state_get();
  pkt[4] = haldex_mitm_get_pump_pct();        // 0..100
  pkt[5] = haldex_mitm_get_target_pct();      // 0..100
  pkt[6] = haldex_mitm_get_vehicle_kmh();
  pkt[7] = haldex_mitm_get_pedal_pct();
  pkt[8] = 0;  // reserved
  pkt[9] = 0;  // reserved

  esp_now_send(BOT32_MAIN_MAC, pkt, PKT_LEN_STATE);
}

void espnow_peer_tick() {
  uint32_t now = millis();
  if (now - g_last_state_tx_ms >= ESPNOW_STATE_INTERVAL_MS) {
    espnow_peer_send_state();
    g_last_state_tx_ms = now;
  }
}

String espnow_peer_get_my_mac() {
  return WiFi.macAddress();
}
