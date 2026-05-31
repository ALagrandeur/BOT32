/*
 * haldex_espnow.cpp — ESP-NOW transport implementation
 *
 * Wire protocol (BOT32-specific, freshly designed):
 *
 *   All packets start with a 2-byte magic header to filter out unrelated
 *   ESP-NOW traffic, followed by a 1-byte type, then a type-specific payload.
 *
 *   Header:  0xBA 0xB0   ("babo" — BOT32 marker)
 *
 *   Packet types:
 *
 *   0x01 STATE (MITM → BOT32):  10 bytes total
 *     [0xBA, 0xB0, 0x01, mode, pump%, target%, kmh, pedal%, rsv0, rsv1]
 *       - mode    : 0..5 (current active mode on the MITM)
 *       - pump%   : current Haldex pump engagement 0..100
 *       - target% : current lock target 0..100
 *       - kmh     : vehicle speed in km/h (single byte, low-res)
 *       - pedal%  : throttle pedal 0..100
 *
 *   0x02 SET_MODE (BOT32 → MITM):  4 bytes total
 *     [0xBA, 0xB0, 0x02, mode]
 *
 *   Add new types here in the future (e.g., 0x03 = PING/PONG, 0x04 = config).
 */
#include "haldex_espnow.h"
#include "haldex_link.h"
#include "settings.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_idf_version.h>

// Arduino-ESP32 Core 3.x (ESP-IDF 5.x) changed the ESP-NOW callback signatures.
//   Core 2.x recv: void(const uint8_t* mac, const uint8_t* data, int len)
//   Core 3.x recv: void(const esp_now_recv_info_t* info, const uint8_t* data, int len)
//   Core 2.x sent: void(const uint8_t* mac, esp_now_send_status_t status)
//   Core 3.x sent: void(const wifi_tx_info_t* tx_info, esp_now_send_status_t status)
#define ESPNOW_NEW_API (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))

// =============================================================
//  Protocol constants
// =============================================================
static const uint8_t MAGIC_0 = 0xBA;
static const uint8_t MAGIC_1 = 0xB0;
#define PKT_TYPE_STATE         0x01
#define PKT_TYPE_SET_MODE      0x02
#define PKT_TYPE_SET_PASSTHRU  0x03   // v3.2.0

#define PKT_LEN_STATE          10
#define PKT_LEN_SET_MODE       4
#define PKT_LEN_SET_PASSTHRU   4       // [BA B0 03 flag]

// =============================================================
//  Internal state
// =============================================================
static bool   g_initialized = false;
static uint8_t g_peer_mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// =============================================================
//  Helpers
// =============================================================
static bool parse_mac_str(const char* str, uint8_t* mac_out) {
  if (!str || strlen(str) < 17) return false;
  for (int i = 0; i < 6; i++) {
    char hex[3] = { str[i*3], str[i*3+1], 0 };
    char* end = nullptr;
    unsigned long v = strtoul(hex, &end, 16);
    if (end == hex || v > 0xFF) return false;
    mac_out[i] = (uint8_t)v;
  }
  return true;
}

static void mac_to_string(const uint8_t* mac, char* out, size_t cap) {
  snprintf(out, cap, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// =============================================================
//  ESP-NOW callbacks — implementation is signature-independent
// =============================================================
static void on_data_recv_impl(const uint8_t* data, int len) {
  // Filter: magic header must match
  if (len < 3) return;
  if (data[0] != MAGIC_0 || data[1] != MAGIC_1) return;

  uint8_t type = data[2];

  if (type == PKT_TYPE_STATE && len >= PKT_LEN_STATE) {
    HaldexState s;
    s.valid                = true;
    s.last_rx_ms           = millis();
    s.current_mode         = data[3];
    s.pump_engagement_pct  = data[4];
    s.lock_target_pct      = data[5];
    s.vehicle_kmh          = data[6];
    s.pedal_pct            = data[7];
    s.passthrough          = data[8];   // v3.2.0: byte 8 = passthrough flag (1/0)
    s.len = (len - 3 > 8) ? 8 : (len - 3);
    for (uint8_t i = 0; i < 8; i++) {
      s.raw[i] = (i < s.len) ? data[3 + i] : 0;
    }
    haldex_link_update_state(s);
  }
  // Future packet types handled here
}

// Bridge functions matching the platform's expected signature
#if ESPNOW_NEW_API
static void on_data_recv_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  (void)info;
  on_data_recv_impl(data, len);
}
static void on_data_sent_cb(const wifi_tx_info_t* tx_info, esp_now_send_status_t status) {
  (void)tx_info; (void)status;
}
#else
static void on_data_recv_cb(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  on_data_recv_impl(data, len);
}
static void on_data_sent_cb(const uint8_t* mac, esp_now_send_status_t status) {
  (void)mac; (void)status;
}
#endif

// =============================================================
//  Public API
// =============================================================
void haldex_espnow_init() {
  if (g_initialized) return;

  // WiFi must be in STA mode for ESP-NOW (no AP connection needed)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("[espnow] init FAILED");
    return;
  }

  esp_now_register_recv_cb(on_data_recv_cb);
  esp_now_register_send_cb(on_data_sent_cb);

  // Determine peer MAC: configured string, or broadcast if empty/invalid
  const Settings& s = settings_get();
  uint8_t mac[6];
  bool use_broadcast = true;
  if (s.haldex_espnow_peer_mac[0] != 0) {
    if (parse_mac_str(s.haldex_espnow_peer_mac, mac)) {
      memcpy(g_peer_mac, mac, 6);
      use_broadcast = false;
    } else {
      Serial.print("[espnow] invalid peer MAC string: ");
      Serial.println(s.haldex_espnow_peer_mac);
    }
  }
  if (use_broadcast) {
    for (int i = 0; i < 6; i++) g_peer_mac[i] = 0xFF;
  }

  // Register peer (required even for broadcast in current esp_now API)
  esp_now_peer_info_t peer_info = {};
  memcpy(peer_info.peer_addr, g_peer_mac, 6);
  peer_info.channel = 0;
  peer_info.encrypt = false;
  if (esp_now_add_peer(&peer_info) != ESP_OK) {
    // If already present, that's OK
    if (!esp_now_is_peer_exist(g_peer_mac)) {
      Serial.println("[espnow] add_peer FAILED");
      return;
    }
  }

  char mac_str[18];
  mac_to_string(g_peer_mac, mac_str, sizeof(mac_str));
  Serial.print("[espnow] init OK, peer=");
  Serial.print(mac_str);
  Serial.print(", my MAC=");
  Serial.println(WiFi.macAddress());

  g_initialized = true;
}

bool haldex_espnow_send_mode(uint8_t mode) {
  if (!g_initialized) {
    haldex_espnow_init();
    if (!g_initialized) return false;
  }
  if (mode > 2) return false;

  uint8_t pkt[PKT_LEN_SET_MODE] = { MAGIC_0, MAGIC_1, PKT_TYPE_SET_MODE, mode };
  esp_err_t r = esp_now_send(g_peer_mac, pkt, PKT_LEN_SET_MODE);
  if (r != ESP_OK) {
    Serial.print("[espnow] send_mode failed, err=0x");
    Serial.println(r, HEX);
    return false;
  }
  return true;
}

bool haldex_espnow_send_passthrough(bool passthrough) {
  if (!g_initialized) {
    haldex_espnow_init();
    if (!g_initialized) return false;
  }
  uint8_t pkt[PKT_LEN_SET_PASSTHRU] = {
    MAGIC_0, MAGIC_1, PKT_TYPE_SET_PASSTHRU, (uint8_t)(passthrough ? 1 : 0)
  };
  esp_err_t r = esp_now_send(g_peer_mac, pkt, PKT_LEN_SET_PASSTHRU);
  if (r != ESP_OK) {
    Serial.print("[espnow] send_passthrough failed, err=0x");
    Serial.println(r, HEX);
    return false;
  }
  return true;
}

String haldex_espnow_get_my_mac() {
  return WiFi.macAddress();
}
