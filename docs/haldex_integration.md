# BOT32 — Haldex AWD link integration guide

> Applies to BOT32 main **v4.8.0** (last stable: v4.6.0) and BOT32-HALDEX **v3.0.0** (the firmware
> in the car; v3.1.0 is an optional update, not used). The AMOLED remote is not used in the Golf.

BOT32 main is the **client**; it drives an external **MITM module** that does the
actual man-in-the-middle work on the private Haldex CAN bus. The devices talk
**only over ESP-NOW** (wireless).

- **BOT32 main** — ESP32 + WaveShare 2-CH CAN HAT. Cluster + OBD + web UI.
- **MITM module** — Autosport Labs **ESP32-CAN-X2**, running the **BOT32-HALDEX**
  firmware (kept in a **private** repo). Sits in series on the Haldex bus.
- **AMOLED remote** (optional 3rd device) — the ESP32-S3 AMOLED 1.8" universal remote of
  [Remote32](https://github.com/ALagrandeur/Remote32) (`Recepteur_AMOLED`, Golf mode). It
  listens to STATE and sends **SET_MODE only** — never SET_PASSTHROUGH, so it cannot arm
  the X2.

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

            ┌──────────────────────────────────────┐
            │  Remote32 AMOLED remote (optional)   │  ESP-NOW (ch.1) to the X2
            │   • Golf mode: sends SET_MODE only   │
            │   • listens to STATE                 │
            └──────────────────────────────────────┘
```

| Device | Role |
|---|---|
| **ESP32-CAN-X2 (BOT32-HALDEX)** | In-line MITM. Reads live state; when armed, rewrites the MQB AWD-demand frames to force the lock. Broadcasts STATE over ESP-NOW. |
| **BOT32 main** | Client. Decides the mode, arms/disarms passthrough, shows live data. No contact with the Haldex bus. |
| **AMOLED remote (Remote32)** | Optional 2nd mode source. Sends SET_MODE (STOCK / FWD / 50-50), never SET_PASSTHROUGH. |

Because the X2 accepts modes from two sources, the main always shows the mode **reported
by the X2** (STATE), and its STOCK button always re-sends an explicit STOCK.

---

## 🔀 Modes (3)

| Mode | # | Effect (when armed) |
|---|---|---|
| **STOCK** | 0 | pass-through, OEM AWD behaviour |
| **FWD**   | 1 | force lock **0%** (front-wheel-drive / burnout) |
| **50/50** | 2 | force lock **100%** (max lock / launch) |

**How modes are set (BOT32 main side, `haldex_modes`):**
- **FWD** arms on the physical combo **Hazards ON + TC OFF** (existing cluster
  sniffers), or from the app/USB button. **v3.10.0:** it **exits to STOCK when
  traction control is turned back ON** (not when the hazards turn off) — so the
  hazards can be switched off while FWD stays armed. Uses the latched TC state
  (0x0FD byte6 == 0x03, stays set while TC is disabled).
- **50/50** (v4.6.0): the **same physical combo with the hand brake PULLED** at the
  moment the combo completes (KOMBI_01 0x30B byte[2] bit 7, sampled once; hand
  brake down = FWD). A combo-armed mode never switches while TC stays OFF
  (re-pressing the hazards does nothing); it exits to STOCK when TC is turned
  back ON. 50/50 is also available from the app/USB button (exits via STOCK).
- **v4.8.0 — only the driver's TC ON releases a combo mode:** a TC OFF → ON change in a
  continuous 0x0FD stream (frames every 20 ms, no gap ≥ 200 ms) with the ignition ON, held
  500 ms. VW turns TC back ON by itself at every ignition, so after a short cut the combo
  mode is kept: leave it with TC OFF then ON, or STOCK. A lost TC frame no longer drops it.
- **No timed auto-revert while driving** (a timed mechanical revert was judged unsafe);
  the only time-based STOCK is after a cut (ignition OFF / bus silent) of 5 min or more.
- The combos and app buttons only work while `haldex_enabled` is ON on the main. The
  sniffed button states must be fresher than 2 s.
- The **AMOLED remote** (Golf mode) can also send STOCK / FWD / 50-50 directly to the X2.

---

## 🔒 Passthrough (MITM arming)

The X2 **persists the passthrough flag in NVS** and restores it at boot (first boot =
passthrough ON = transparent bridge). The **mode always boots in STOCK** (not persisted),
and since BOT32 v4.8.0 it is also **put back to STOCK after a cut of 5 min or more** by BOT32
(ignition OFF or CAN bus silent for 5 min, or a full BOT32 power-up; a shorter cut keeps the
mode; re-sent until the X2 reports STOCK, also while the X2 sleeps — only while
`haldex_enabled` is ON). With
X2 v3.0.0 (the firmware in the car) this relies entirely on BOT32: the X2 does not reboot
with the ignition and keeps its mode on its own. The optional X2 v3.1.0 would add a
fallback to STOCK after 5 s without CAN.
So nothing is modified at X2 power-up even if the X2 was switched off armed (honours "no
guessed frame emitted by default"). BOT32 main must explicitly send
**SET_PASSTHROUGH OFF** to arm the MITM (UI has a modal confirmation). Only BOT32's own
UIs can arm (phone, or PC — which in USB-C development mode sends the command straight to
the X2); the AMOLED remote cannot. Only when **passthrough is OFF AND mode ≠ STOCK** does the X2 rewrite frames.

The live UI shows the **actual** passthrough state reported by the X2.

---

## 📡 ESP-NOW wire protocol (BOT32-specific)

Channel: both devices **hard-lock the radio to WiFi channel 1** via
`esp_wifi_set_channel()` (so the phone AP and the link coexist). Pairing:
broadcast by default; set the peer MAC on each side to lock.

> **Reliability (v3.9.0 / v1.5.0):** both ends MUST disable WiFi modem power-save
> (`WiFi.setSleep(false)` + `esp_wifi_set_ps(WIFI_PS_NONE)`). In STA mode the radio
> otherwise naps between beacons and DROPS incoming ESP-NOW packets — this was the
> root cause of a dead link even with channels matched. The main re-asserts PS-off
> after switching to AP+STA (which silently re-enables it).

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
bytes and recomputes the **E2E CRC** (AUTOSAR CRC8, poly 0x2F, init/xorout 0xFF).

> **CRC construction (confirmed 100 % on 5 real Haldex-bus logs):** computed over
> `[D2,D3,D4,D5,D6,D7,D8, DataID]` — the **DataID is APPENDED**, not prepended
> (prepend matched only ~1 %). Alive counter = `D2 & 0x0F`. Per-ID DataID tables:
> 0x08A=all 0xD4, **0x116=all 0xAC**, 0x106=all 0x07, plus the 0x0A7/0x0A8 tables.

The Gen5 Haldex is **feed-forward** (engine-torque-driven, not slip-reactive) AND it
**de-rates the lock as vehicle speed rises**. So 50/50 pins the torque lever AND makes
the Haldex believe the car is **stationary + idling** so it never de-rates. FWD is
unchanged.

| Frame | ID | 50/50 role |
|---|---|---|
| ESP_14   | `0x08A` | D8 ceiling pinned to the car's real max (`0xFA`; `0xFE` is never produced by this car) |
| MOTOR_11 | `0x0A7` | D4 = engine torque pinned high — the lock **lever** |
| ESP_21   | `0x0FD` | **vehicle speed → 0** — defeats the speed de-rate (**the key fix**) |
| ESP_19   | `0x0B2` | wheel speeds → 0 (consistent "stopped"; no CRC/counter) |
| MOTOR_12 | `0x0A8` | D7 engine-RPM → low (idle; high RPM also de-rates) |
| MOTOR_14 | `0x3BE` | **NOT touched** — braking-while-rolling = 0 % engagement, so faking the brake would kill the lock |

(FWD → demand `0x00` + wheels 0. Exact byte values + the per-counter DataID tables
live in the private BOT32-HALDEX firmware.)

> **The breakthrough (v3.0.0).** Earlier builds forwarded the real speed, so 50/50
> faded from ~95 % at standstill to ~0 % by 60 km/h. Making the Haldex believe it is
> stationary (speed/wheels/RPM neutralised, like OpenHaldex) holds **~95 % at ALL
> speeds** — confirmed in-vehicle. Only steering angle still sheds some lock (the
> Haldex's own corner behaviour, **kept on purpose** — a fully-locked coupling binds
> the driveline in a turn). The clutch oil temp (UDS **0x2BF1**, `temp_C =
> data[5]×2.7−241.5`, calibrated vs VCDS) is **displayed** in the mobile UI for
> awareness — BOT32 adds **no over-temp cut-out**: the Haldex runs its **own** thermal
> protection, which is sufficient. **Closed course, short sessions**; validate starting
> from passthrough ON.

---

## ⚙️ BOT32 main settings (Haldex)

| Setting | Default | Description |
|---|---|---|
| `haldex_enabled` | `false` | Master toggle for the ESP-NOW link. Also required for the v4.8.0 STOCK after a 5-min cut — with X2 v3.0.0 it is the only restart safety net, so keep it ON in the car. Switching it OFF sends a last confirmed STOCK |
| `haldex_espnow_peer_mac` | empty (broadcast) | MAC of the X2 to lock the pairing |

(The old CAN-transport settings `haldex_bus` / `haldex_state_id` / `haldex_cmd_id`
/ `haldex_transport` were removed in v3.2.0 — ESP-NOW is the only transport.)

---

## 📋 Troubleshooting

| Symptom | Likely cause |
|---|---|
| Link banner red, `espnow_rx = 0` | WiFi power-save still on (flash v3.9.0/v1.5.0), wrong channel, or X2 off/asleep |
| Live state shows "déconnecté" | X2 not powered or asleep (>15 min parked, no USB). The main relays ESP-NOW on its own and the phone UI works without a PC; only the **PC UI** needs the main connected over USB (or, in USB-C dev mode, the X2 connected to the PC) |
| Modes/passthrough don't take effect | `haldex_enabled` is OFF on the main, or X2 offline. PC UI only: main not connected over USB (ESP-NOW source) or X2 not connected (USB-C source) |
| Still FWD / 50-50 after a restart | Normal after a cut shorter than 5 min (the mode is kept): TC OFF then ON, or 🅾 STOCK. After a longer cut: check `haldex_enabled` ON and BOT32 on permanent power (X2 v3.0.0 has no fallback of its own). BOT32 keeps re-sending STOCK until the X2 reports it (30 min max without any X2 STATE); if the LED keeps blinking the X2 link is down — press 🅾 STOCK once it is back |
| FWD / 50-50 selected but nothing happens | X2 not armed (badge 🟢 = passthrough ON) — arm it from the main's app |
| Mode changed without touching the main | the AMOLED remote (Golf mode) sent a SET_MODE — the main shows the mode reported by the X2 |
| Armed but no AWD change | passthrough still ON, or this controller variant uses a different demand byte (validate on bench) |
| TC/TPMS faults at boot or after idle | in-series boot gap (fixed by FreeRTOS bridge), or marginal termination — enable **TERM2 (120 Ω)** on the Haldex side and watch the MCP TEC stay 0 |

> **Bench diagnostics:** read the X2 serial `[5s]` line — `espnow ch=` must be `1`,
> `tx=` should climb (X2 radiating), `rxcmd=` climbs when you click a mode (X2 hears
> the main). Keep the X2 on **USB-C** on the bench so it never light-sleeps.

---

## 📜 License

- **BOT32** (public) — MIT.
- **BOT32-HALDEX** (private) — MIT, implemented from protocol facts only.
- **OpenHaldex-C6** (separate, not included) — FASL v1.0. Respect its license.
