/*
 * can_handler.cpp — implementation
 *
 * Pattern: 2x ACAN2515 instances on shared SPI, each with its own CS + INT.
 * Same idea as BOT32 main (MIT-licensed), adapted here for the MITM API
 * (separate accessors per side, no channel enum, no listener registry —
 * the main loop drains each side in tight loop for minimum latency).
 */
#include "can_handler.h"
#include "config.h"
#include <ACAN2515.h>
#include <SPI.h>

// =============================================================
//  Instances on shared SPI
// =============================================================
static ACAN2515 mcp_pcm(PIN_PCM_CS,    SPI, PIN_PCM_INT);
static ACAN2515 mcp_hdx(PIN_HALDEX_CS, SPI, PIN_HALDEX_INT);

// =============================================================
//  Stats
// =============================================================
static CanSideStats s_pcm_stats = { 0, 0, 0 };
static CanSideStats s_hdx_stats = { 0, 0, 0 };

// =============================================================
//  Init
// =============================================================
static bool init_chip(ACAN2515& chip, const char* name) {
  ACAN2515Settings settings(
    (uint32_t)MCP2515_CLOCK_MHZ * 1000000UL,
    HALDEX_BUS_BITRATE
  );
  settings.mRequestedMode = ACAN2515Settings::NormalMode;

  uint16_t err;
  if (&chip == &mcp_pcm) {
    err = chip.begin(settings, [] { mcp_pcm.isr(); });
  } else {
    err = chip.begin(settings, [] { mcp_hdx.isr(); });
  }

  if (err != 0) {
    Serial.print("[CAN] ");
    Serial.print(name);
    Serial.print(" init FAILED, err=0x");
    Serial.println(err, HEX);
    return false;
  }
  Serial.print("[CAN] ");
  Serial.print(name);
  Serial.print(" started @ ");
  Serial.print(HALDEX_BUS_BITRATE);
  Serial.print(" bps, xtal ");
  Serial.print(MCP2515_CLOCK_MHZ);
  Serial.println(" MHz");
  return true;
}

bool can_init() {
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
  bool ok = true;
  ok &= init_chip(mcp_pcm, "PCM side");
  ok &= init_chip(mcp_hdx, "Haldex side");
  return ok;
}

// =============================================================
//  PCM side accessors
// =============================================================
bool can_pcm_available() {
  return mcp_pcm.available();
}

bool can_pcm_receive(CanFrame& f) {
  CANMessage rx;
  if (!mcp_pcm.available()) return false;
  mcp_pcm.receive(rx);
  f.id = rx.id;
  f.len = rx.len;
  for (uint8_t i = 0; i < 8; i++) f.data[i] = (i < f.len) ? rx.data[i] : 0;
  s_pcm_stats.rx++;
  return true;
}

bool can_pcm_send(const CanFrame& f) {
  CANMessage msg;
  msg.id = f.id;
  msg.len = f.len;
  msg.ext = false;
  msg.rtr = false;
  for (uint8_t i = 0; i < f.len; i++) msg.data[i] = f.data[i];
  if (mcp_pcm.tryToSend(msg)) {
    s_pcm_stats.tx_ok++;
    return true;
  }
  s_pcm_stats.tx_fail++;
  return false;
}

// =============================================================
//  Haldex side accessors
// =============================================================
bool can_haldex_available() {
  return mcp_hdx.available();
}

bool can_haldex_receive(CanFrame& f) {
  CANMessage rx;
  if (!mcp_hdx.available()) return false;
  mcp_hdx.receive(rx);
  f.id = rx.id;
  f.len = rx.len;
  for (uint8_t i = 0; i < 8; i++) f.data[i] = (i < f.len) ? rx.data[i] : 0;
  s_hdx_stats.rx++;
  return true;
}

bool can_haldex_send(const CanFrame& f) {
  CANMessage msg;
  msg.id = f.id;
  msg.len = f.len;
  msg.ext = false;
  msg.rtr = false;
  for (uint8_t i = 0; i < f.len; i++) msg.data[i] = f.data[i];
  if (mcp_hdx.tryToSend(msg)) {
    s_hdx_stats.tx_ok++;
    return true;
  }
  s_hdx_stats.tx_fail++;
  return false;
}

// =============================================================
//  Stats
// =============================================================
CanSideStats can_pcm_stats()    { return s_pcm_stats; }
CanSideStats can_haldex_stats() { return s_hdx_stats; }
