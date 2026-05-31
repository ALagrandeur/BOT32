# BOT32 — Haldex AWD link integration guide

BOT32 main is the **client**; it drives an external **MITM module** that does the
actual man-in-the-middle work on the private Haldex CAN bus. The two devices talk
**only over ESP-NOW** (wireless).

- **BOT32 main** — ESP32 + WaveShare 2-CH CAN HAT. Cluster + OBD + web UI.
- **MITM module** — Autosport Labs **ESP32-CAN-X2**, running the **BOT32-HALDEX**
  firmware (kept in a **private** repo). Sits in series on the Haldex bus.

> ⚠️ **MOTORSPORT — closed course only.** Forcing the AWD lock is for drag/track
> use. Do not use on public roads.

---

## 🙏 Attribution

The MQB Haldex protocol knowledge (which frames carry the AWD demand, the E2E
CRC scheme, the mode concept) was openly reverse-engineered by the **OpenHaldex**
community (Forbes Automotive — OpenHaldex-C6, FASL v1.0). **No source code from
OpenHaldex is included** in BOT32 or BOT32-HALDEX: both implement only the
**protocol facts** (CAN IDs, byte positions, the standard AUTOSAR CRC8), which
are not copyrightable. The BOT32-HALDEX firmware is kept private (personal use).

---

## 🏗 Architecture

```
   Vehicle (MK7 Alltrack 4Motion, MQB Gen5 / 0CQ Haldex)

        PCM / ESP / gateway ──┐
                              │  Haldex CAN segment (cut in series)
            ┌─────────────────┴───────────────────┐
            │  ESP32-CAN-X2  (BOT32-HALDEX)        │
            │   CAN1 (TWAI)   = CAR side           │
            │   CAN2 (MCP2515)= Haldex side        │
            │   • bridge CAR <-> Haldex            │
            │   • passthrough ON  = transparent    │
            │   • passthrough OFF = MITM armed     │
            └─────────────────┬───────────────────┘
                              │  ESP-NOW (WiFi ch.1)
            ┌─────────────────┴───────────────────┐
            │  BOT32 main (ESP32 + CAN HAT)        │
            │   • web UI (USB) + phone AP (WiFi)   │
            │   • sends SET_MODE + SET_PASSTHROUGH │
            │   • shows live STATE                 │
            └──────────────────────────────────────┘
```

| Device | Role |
|---|---|
| **ESP32-CAN-X2 (BOT32-HALDEX)** | In-line MITM. Reads live state; when armed, rewrites the MQB AWD-demand frames to force the lock. Broadcasts STATE over ESP-NOW. |
| **BOT32 main** | Client. Decides the mode, arms/disarms passthrough, shows live data. No contact with the Haldex bus. |

---

## 🔀 Modes (3)

| Mode | # | Effect (when armed) |
|---|---|---|
| **STOCK** | 0 | pass-through, OEM AWD behaviour |
| **FWD**   | 1 | force lock **0%** (front-wheel-drive / burnout) |
| **50/50** | 2 | force lock **100%** (max lock / launch) |

**How modes are set (BOT32 main side, `haldex_modes`):**
- **FWD** arms on the physical combo **Hazards ON + TC button** (existing cluster
  sniffers), or from the app/USB button. It **exits to STOCK when the hazards
  turn OFF**.
- **50/50** is app/USB only; exits via the STOCK button.
- **No timed auto-revert** (a timed mechanical revert was judged unsafe).

---

## 🔒 Passthrough (MITM arming)

The X2 always **boots in passthrough ON** = transparent bridge, nothing modified
(honours "no guessed frame emitted by default"). BOT32 main must explicitly send
**SET_PASSTHROUGH OFF** to arm the MITM (UI has a modal confirmation). Only when
**passthrough is OFF AND mode ≠ STOCK** does the X2 rewrite frames.

The live UI shows the **actual** passthrough state reported by the X2.

---

## 📡 ESP-NOW wire protocol (BOT32-specific)

Channel: both devices locked to **WiFi channel 1** (so the phone AP and the link
coexist). Pairing: broadcast by default; set the peer MAC on each side to lock.

```
   Magic: 0xBA 0xB0     (filter)
   Type:  1 byte
```

**0x01 STATE (X2 → main), 10 bytes**
```
 0 0xBA   1 0xB0   2 0x01
 3 mode (0..2)         4 pump_pct (0..100)
 5 target_pct (0..100) 6 kmh        7 pedal_pct
 8 passthrough (1=ON/transparent, 0=armed)   9 reserved
```

**0x02 SET_MODE (main → X2), 4 bytes**: `[0xBA 0xB0 0x02 mode(0..2)]`

**0x03 SET_PASSTHROUGH (main → X2), 4 bytes**: `[0xBA 0xB0 0x03 flag(1=ON/0=armed)]`

---

## 🔧 MITM frame modification (X2, Phase 2)

MQB Gen5 has no single "lock" frame — the Haldex (0CQ) derives its lock from the
powertrain/ESP torque-vectoring frames. When armed, the X2 rewrites the AWD-demand
byte and recomputes the **E2E CRC** (AUTOSAR CRC8, poly 0x2F, init/xorout 0xFF),
keeping the sender's 4-bit alive counter:

| Frame | ID | Byte(s) rewritten |
|---|---|---|
| ESP_14   | `0x08A` | data[7] (BR_Vorg_Allrad_Max) — primary |
| MOTOR_11 | `0x0A7` | data[6], data[7] (torque request) |
| MOTOR_12 | `0x0A8` | data[7] |

Demand byte: FWD → `0x00` (lock 0%), 50/50 → `0xFE` (lock 100%).

> The exact byte that drives a given controller variant is empirically tuned in
> the OpenHaldex community; this implementation follows the documented Gen5
> strategy. Validate on a closed course, starting from passthrough ON.

---

## ⚙️ BOT32 main settings (Haldex)

| Setting | Default | Description |
|---|---|---|
| `haldex_enabled` | `false` | Master toggle for the ESP-NOW link |
| `haldex_espnow_peer_mac` | empty (broadcast) | MAC of the X2 to lock the pairing |

(The old CAN-transport settings `haldex_bus` / `haldex_state_id` / `haldex_cmd_id`
/ `haldex_transport` were removed in v3.2.0 — ESP-NOW is the only transport.)

---

## 📋 Troubleshooting

| Symptom | Likely cause |
|---|---|
| Live state shows "déconnecté" | X2 not powered, not paired, or not on WiFi channel 1 |
| Modes/passthrough don't take effect | `haldex_enabled` is OFF, or peer MAC mismatch |
| Armed but no AWD change | passthrough still ON, or this controller variant uses a different demand byte (validate on bench) |
| CAN errors on the X2 | termination: disable both on-board 120 Ω (TERM1/TERM2) — OEM terminators remain at each cut end |

---

## 📜 License

- **BOT32** (public) — MIT.
- **BOT32-HALDEX** (private) — MIT, implemented from protocol facts only.
- **OpenHaldex-C6** (separate, not included) — FASL v1.0. Respect its license.
