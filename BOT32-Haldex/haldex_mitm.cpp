/*
 * haldex_mitm.cpp — MK7 Alltrack Gen 5 implementation skeleton
 *
 * Status: STUB scaffolding with all known CAN IDs in place. Three TODO blocks
 * remain — fill them in from your own notes (taken while reading your private
 * OpenHaldex-C6 fork) without copying source code verbatim.
 *
 * What's pre-filled (free facts, not code):
 *   - All relevant CAN IDs (MK7 MQB Haldex Gen 5)
 *   - HALDEX_GEN5 status frame parsing (byte 2 = engagement raw 0..250)
 *   - State byte (data[3]) bit assignments
 *   - Per-frame rolling counter ranges
 *
 * What's left for you (read your fork's Calculations.cpp lines 405-522
 * and re-implement in your own words):
 *   - TODO #1A — exact mapping of byte values for each mode on each frame
 *   - TODO #1B — extend vw_mqb.h with CRC seeds for IDs 0x08A, 0x0A7,
 *               0x0A8 (and optionally 0x0B2 if you want wheel-speed faking)
 *   - TODO #2  — vehicle speed extraction (if you want it in the UI)
 *
 * IMPORTANT — protect this file from git before you fill it in:
 *   git update-index --skip-worktree BOT32-Haldex/haldex_mitm.cpp
 *
 * Once protected, your filled-in version stays local and never gets pushed
 * to the public MIT repo (avoiding FASL / MIT license mixing).
 */
#include "haldex_mitm.h"
#include "mode_state.h"

// =============================================================
//  CAN IDs — VW MQB Haldex Gen 5 (MK7 4Motion / Alltrack)
//  These are facts of the MQB protocol, not OpenHaldex code.
// =============================================================

// PCM → Haldex (the frames we MAY modify, depending on mode)
#define ID_ESP_14   0x08A   // ★ BR_Vorg_Allrad_Max in byte 7 — MOST CRITICAL
#define ID_MOTOR_11 0x0A7   // bytes 6 & 7 = torque demand
#define ID_MOTOR_12 0x0A8   // byte 7 = engine speed quality (minor effect)
#define ID_ESP_19   0x0B2   // bytes 0-7 = 4 wheel speeds (optional)

// Haldex → PCM (the frames we READ to extract live state)
#define ID_HALDEX_GEN5_STATUS 0x118   // ★ pump engagement (byte 2), state bits (byte 3)

// State byte (HALDEX_GEN5_STATUS data[3]) bit assignments
#define HDX_STATE_CLUTCH1_REPORT_BIT  0
#define HDX_STATE_TEMP_PROTECTION_BIT 1   // ⚠ alarm — pump overtemp, surface in UI
#define HDX_STATE_CLUTCH2_REPORT_BIT  2
#define HDX_STATE_COUPLING_OPEN_BIT   3
#define HDX_STATE_SPEED_LIMIT_BIT     6

// "Neutral" demand byte values (when the OEM PCM is asking for nothing
// special). Used as the base value we scale or zero out depending on mode.
#define DEMAND_NEUTRAL_FA 0xFA
#define DEMAND_NEUTRAL_FE 0xFE

// Per-frame MQB rolling counter ranges (low nibble = counter, high nibble = fixed)
#define ESP_14_CTR_LO   0x10
#define ESP_14_CTR_HI   0x1F
#define MOTOR_11_CTR_LO 0x40
#define MOTOR_11_CTR_HI 0x4F
#define MOTOR_12_CTR_LO 0x70
#define MOTOR_12_CTR_HI 0x7F

// =============================================================
//  Cached live state — populated by haldex_mitm_process_haldex_frame()
// =============================================================
static uint8_t g_pump_pct      = 0;
static uint8_t g_target_pct    = 0;
static uint8_t g_vehicle_kmh   = 0;
static uint8_t g_pedal_pct     = 0;
static uint8_t g_haldex_flags  = 0;   // bitfield, mirrors HALDEX status data[3]

// Per-ID counter trackers (we maintain our own so MITM-rewritten frames
// have valid rolling counters even when we replace the payload)
static uint8_t g_esp14_ctr   = ESP_14_CTR_LO;
static uint8_t g_motor11_ctr = MOTOR_11_CTR_LO;
static uint8_t g_motor12_ctr = MOTOR_12_CTR_LO;

static inline uint8_t advance_ctr(uint8_t cur, uint8_t lo, uint8_t hi) {
  cur++;
  if (cur > hi) cur = lo;
  return cur;
}

// =============================================================
//  Helper: compute the demand byte for the current mode
//  Returns the byte to write into the frame's demand position.
// =============================================================
static uint8_t demand_byte_for_mode(uint8_t mode, uint8_t neutral_value) {
  switch (mode) {
    case 0: // STOCK — should never reach here (we don't modify in stock)
      return neutral_value;

    case 1: // FWD — pump 0% — force minimum demand
      return 0x00;

    case 2: // 5050 — pump 100% — let max demand reach Haldex
      return neutral_value;

    case 3: // 6040 — TODO #1A: pick a scaled value (~60% of neutral)
      return (uint16_t)neutral_value * 60 / 100;

    case 4: // 7525 — TODO #1A: pick a scaled value (~25% of neutral)
      return (uint16_t)neutral_value * 25 / 100;

    case 5: // EXPERT — user-defined; for now, passthrough
      return neutral_value;

    default:
      return neutral_value;
  }
}

void haldex_mitm_init() {
  Serial.println("[haldex_mitm] Gen5 MK7 scaffolding loaded");
  Serial.println("[haldex_mitm] PCM->HDX modify IDs: 0x08A, 0x0A7, 0x0A8");
  Serial.println("[haldex_mitm] HDX->PCM read ID:    0x118");
  Serial.println("[haldex_mitm] Remember: extend vw_mqb.h with CRC seeds for these IDs!");
}

// =============================================================
//  PCM -> Haldex frame processing
//  (we may modify the frame in place before forwarding)
// =============================================================
bool haldex_mitm_process_pcm_frame(CanFrame& f) {
  uint8_t mode = mode_state_get();

  // STOCK: never modify anything — pure passthrough for safety.
  if (mode == 0) return true;

  switch (f.id) {

    // ─────────────────────────────────────────────────────────
    // ESP_14 (0x08A) — THE critical frame. Byte 7 = BR_Vorg_Allrad_Max
    // ─────────────────────────────────────────────────────────
    case ID_ESP_14: {
      // TODO #1A — fill in if you want to override more bytes than just 7.
      //   Reading your fork's Calculations.cpp ~lines 504-522 will tell you
      //   if any of bytes 2-6 also need overriding for any mode.
      f.data[7] = demand_byte_for_mode(mode, DEMAND_NEUTRAL_FE);

      // Maintain our own rolling counter (overwrites OEM counter — that's OK
      // for MITM as long as it stays monotonic within the valid range).
      f.data[1] = g_esp14_ctr;
      g_esp14_ctr = advance_ctr(g_esp14_ctr, ESP_14_CTR_LO, ESP_14_CTR_HI);

      // TODO #1B — recalculate MQB CRC8H2F into data[0].
      //   Extend BOT32/vw_mqb.cpp with the seed for ID 0x08A, then call:
      //     f.data[0] = vw_mqb_crc(0x08A, f.data, 8);
      //   Until that's wired in, leaving the OEM CRC will cause the Haldex
      //   to ignore the frame (safe default — falls back to last good value).
      // f.data[0] = vw_mqb_crc(ID_ESP_14, f.data, 8);
      break;
    }

    // ─────────────────────────────────────────────────────────
    // MOTOR_11 (0x0A7) — bytes 6 & 7 = filtered torque demand
    // ─────────────────────────────────────────────────────────
    case ID_MOTOR_11: {
      f.data[6] = demand_byte_for_mode(mode, DEMAND_NEUTRAL_FA);
      f.data[7] = demand_byte_for_mode(mode, DEMAND_NEUTRAL_FA);

      f.data[1] = g_motor11_ctr;
      g_motor11_ctr = advance_ctr(g_motor11_ctr, MOTOR_11_CTR_LO, MOTOR_11_CTR_HI);

      // TODO #1B — see ESP_14 above.
      // f.data[0] = vw_mqb_crc(ID_MOTOR_11, f.data, 8);
      break;
    }

    // ─────────────────────────────────────────────────────────
    // MOTOR_12 (0x0A8) — byte 7 = engine speed quality (minor)
    // Optional: only modify if you observe a benefit on bench.
    // ─────────────────────────────────────────────────────────
    case ID_MOTOR_12: {
      // Minor effect — leaving it passthrough is usually fine.
      // Uncomment the next 3 lines + add CRC if testing shows it helps.
      //
      // f.data[7] = demand_byte_for_mode(mode, DEMAND_NEUTRAL_FA);
      // f.data[1] = g_motor12_ctr;
      // g_motor12_ctr = advance_ctr(g_motor12_ctr, MOTOR_12_CTR_LO, MOTOR_12_CTR_HI);
      // f.data[0] = vw_mqb_crc(ID_MOTOR_12, f.data, 8);
      break;
    }

    // ─────────────────────────────────────────────────────────
    // ESP_19 (0x0B2) — wheel speeds.
    //   Falsifying these can help force FWD mode by making the Haldex
    //   think the rear wheels are spinning at the same speed as front
    //   (no slip → no demand). OPTIONAL — only enable if ESP_14 + MOTOR_11
    //   alone don't fully kill rear engagement during burnout.
    // ─────────────────────────────────────────────────────────
    case ID_ESP_19: {
      // Default: passthrough (don't touch wheel speeds — they're also
      // used by other ECUs and falsifying may trigger faults).
      break;
    }

    default:
      // Any other frame passes through unmodified.
      break;
  }

  return true;  // forward to Haldex side
}

// =============================================================
//  Haldex -> PCM frame processing (state extraction)
// =============================================================
bool haldex_mitm_process_haldex_frame(CanFrame& f) {

  if (f.id == ID_HALDEX_GEN5_STATUS && f.len >= 4) {
    // data[2] = engagement raw, scale 0..250 -> 0..100 %
    uint8_t raw = f.data[2];
    uint8_t pct = (raw <= 250) ? (uint16_t)raw * 100 / 250 : 100;
    g_pump_pct = pct;

    // data[3] = state bitfield (see HDX_STATE_*_BIT defines above)
    g_haldex_flags = f.data[3];

    // Target % — OpenHaldex doesn't seem to broadcast target separately
    // for Gen5; we approximate by storing what the active mode is asking for.
    uint8_t mode = mode_state_get();
    switch (mode) {
      case 0: g_target_pct = pct;  break;  // STOCK: target = actual
      case 1: g_target_pct = 0;    break;  // FWD
      case 2: g_target_pct = 100;  break;  // 5050
      case 3: g_target_pct = 60;   break;
      case 4: g_target_pct = 25;   break;
      case 5: g_target_pct = pct;  break;  // EXPERT: target = actual
    }
  }

  // TODO #2 — extract vehicle speed (kmh) from ESP_19 wheel speeds.
  //   ESP_19 (0x0B2) carries 4 wheel speed signals (data bytes 0-7).
  //   MQB convention: each wheel speed is a 16-bit unsigned little-endian
  //   value, raw units = 0.01 km/h. Average the 4 wheels (or use just
  //   front wheels for AWD-independent reading).
  //
  //   if (f.id == ID_ESP_19) {
  //     uint16_t fl = f.data[4] | (f.data[5] << 8);
  //     uint16_t fr = f.data[6] | (f.data[7] << 8);
  //     uint16_t avg = (fl + fr) / 2;
  //     g_vehicle_kmh = (uint8_t) min<uint16_t>(255, avg / 100);
  //   }
  //
  //   Verify the byte order + scaling on bench with a CAN sniffer before
  //   trusting it — MQB conventions vary slightly between platforms.

  return true;  // forward to PCM side
}

// =============================================================
//  Cached state accessors
// =============================================================
void haldex_mitm_set_pump_pct(uint8_t v)    { g_pump_pct = v; }
void haldex_mitm_set_target_pct(uint8_t v)  { g_target_pct = v; }
void haldex_mitm_set_vehicle_kmh(uint8_t v) { g_vehicle_kmh = v; }
void haldex_mitm_set_pedal_pct(uint8_t v)   { g_pedal_pct = v; }

uint8_t haldex_mitm_get_pump_pct()    { return g_pump_pct; }
uint8_t haldex_mitm_get_target_pct()  { return g_target_pct; }
uint8_t haldex_mitm_get_vehicle_kmh() { return g_vehicle_kmh; }
uint8_t haldex_mitm_get_pedal_pct()   { return g_pedal_pct; }
