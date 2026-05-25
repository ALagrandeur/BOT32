# BOT32 — Haldex AWD link integration guide

BOT32 implements only the **client side** of a Haldex MITM architecture:
it sends mode commands over CAN and reads a state broadcast emitted by an
external module that does the actual man-in-the-middle work on the Haldex
bus.

This document covers:
- The architecture (BOT32 ↔ external MITM module)
- The CAN protocol BOT32 expects
- How to set up the external module
- Attribution and licensing

---

## 🙏 Attribution — credit where it's due

The CAN protocol described here (state broadcast on a configurable ID,
mode command frame) was originally designed and publicly documented by
**Forbes Automotive** for their **OpenHaldex-C6** project:

→ https://github.com/Forbes-Automotive/OpenHaldex-C6

OpenHaldex-C6 is distributed under the **Forbes Automotive Source-Available
License v1.0 (FASL)**. It is **not** an open-source license in the
traditional sense — it permits personal, educational, and non-commercial
use only.

**BOT32 does NOT include any source code, PCB designs, or compiled binaries
from OpenHaldex-C6.** The BOT32 code that talks to the protocol
(`BOT32/haldex_link.h/cpp` and related UI) is freshly written from scratch
under MIT license, using only the publicly documented protocol facts
(CAN message IDs, mode numbering scheme) which are facts and not copyrightable.

Huge thanks to Forbes Automotive for the open reverse-engineering work
on the VW MQB Haldex platform.

---

## 🏗 Architecture

```
   ┌──────────────────────────────────────────────────────┐
   │ Vehicle MK7 4Motion                                   │
   │                                                       │
   │  PCM ──── chassis CAN (500 kbps) ──┬── OBD2 port      │
   │                                    │                  │
   │                                    └── external MITM  │
   │                                        module (ESP32) │
   │                                          │            │
   │                                          ├─ Haldex CAN│
   │                                          │  (private) │
   │                                          ↓            │
   │                                       Haldex AWD ECU  │
   │                                                       │
   └──────────────────────────────────────────────────────┘
                                       ▲
                                       │ chassis CAN
                                       │
   ┌──────────────────────────────────────────────────────┐
   │ BOT32 ESP32 (our existing device)                     │
   │                                                       │
   │  CAN1 (OBD2) ←─→ chassis CAN                          │
   │                                                       │
   │  Reads: state broadcast on configurable ID (0x6B0)    │
   │  Sends: mode commands on configurable ID (0x6B1)      │
   │                                                       │
   │  Web UI: 3 race-mode buttons + live state display     │
   └──────────────────────────────────────────────────────┘
```

**Two devices, two roles:**

| Device | Role |
|---|---|
| **External MITM module** | Sits between PCM and Haldex AWD ECU on the private Haldex CAN bus. Forwards frames most of the time, modifies the demand frame when in a non-stock mode. Broadcasts its state on chassis CAN for clients to monitor. |
| **BOT32** | Client. No direct contact with the Haldex bus. Sends "set mode N" commands and listens to the periodic state broadcast. Provides the web UI + steering-button trigger (future). |

---

## 📡 Protocol expected by BOT32

### State broadcast (MITM → BOT32)

- **CAN ID**: configurable, defaults to `0x6B0` (matches the public OpenHaldex
  convention)
- **Length**: 8 bytes
- **Rate**: ~10 Hz (any rate works, BOT32 just stores the latest)
- **Content** (default byte positions, adjustable in `BOT32/haldex_link.cpp`):
  - one byte for current pump engagement (0..100)
  - one byte for current lock target (0..100)
  - one byte for vehicle speed (km/h, low-res)
  - one byte for current active mode (0..5)
  - one byte for pedal position (0..100)

If your MITM module uses a different layout, edit
`DEFAULT_POS_*` constants in `haldex_link.cpp`.

### Mode command (BOT32 → MITM)

- **CAN ID**: configurable, defaults to `0x6B1`
  - Note: the public OpenHaldex documentation describes mode commands but
    does not explicitly state which CAN ID receives them. The `0x6B1`
    default (one above the state broadcast) is a reasonable convention;
    adjust to whatever your MITM module listens for.
- **Length**: 8 bytes
- **Content**: mode number in byte 0, remaining bytes zero
- **Mode numbers**:
  - `0` = Stock (pass-through, OEM behavior)
  - `1` = FWD (force pump 0% — burnout / pre-stage tire warm-up)
  - `2` = 5050 (force pump 100% — launch / max grip)
  - `3` = 60/40
  - `4` = 75/25
  - `5` = Expert (MITM module's user-defined behavior)

---

## 🛠 Setting up the external MITM module

You have two clean options. Both keep BOT32 (this repo) clean of any
FASL-licensed material:

### Option A — Buy/build the official OpenHaldex-C6 hardware

- PCB Gerbers, schematics, and firmware are in the OpenHaldex-C6 repo
- Per FASL v1.0, personal/non-commercial use is permitted — you can
  build and run it yourself
- Once installed in your car, BOT32 sees its broadcast on chassis CAN
  and can send mode commands

### Option B — Fork OpenHaldex-C6 for your own ESP32 hardware (personal use)

If you have a custom ESP32 + 2 CAN modules setup (as discussed):
- **Fork OpenHaldex-C6 to your own GitHub account** (keep the fork respecting
  FASL: personal use, attribution, no commercial sale)
- Adapt the firmware to your hardware's pin assignments
- Your fork stays in your namespace, separate from BOT32

⚠ **Do not** copy OpenHaldex-C6 source code into the BOT32 public repo
(BOT32 is MIT-licensed, and FASL forbids inclusion in incompatibly-licensed
distributions).

### Option C — Use the BOT32 web UI as a generic client

The BOT32 client side works with **any** device that emits the same CAN
broadcast/command protocol — OpenHaldex-C6, a fork of it, or your own
implementation. If you write your own MITM firmware (e.g., for the
2-CAN-modules ESP32 you mentioned), just make sure it:
- Broadcasts state on `0x6B0` (or your chosen ID, configurable)
- Listens for `0x6B1` (or your chosen ID) for incoming mode commands
- Uses byte positions matching the defaults (or update `haldex_link.cpp`)

---

## ⚙️ BOT32 settings for the Haldex link

In the BOT32 web UI under **Configuration → 🏁 Haldex AWD link** :

| Setting | Default | Description |
|---|---|---|
| `haldex_enabled` | `false` | Master toggle — must be ON to TX commands or count RX |
| `haldex_bus` | `1` (OBD2) | Which CAN channel the MITM is on (0 = CAN0 cluster, 1 = CAN1 OBD2) |
| `haldex_state_id` | `0x6B0` | CAN ID to listen for state broadcast |
| `haldex_cmd_id` | `0x6B1` | CAN ID to use for outgoing mode commands |

All persisted in NVS. Change via UI → auto-saved.

---

## 🏁 Race-day workflow (BOT32 + MITM together)

```
1. Stage at burnout box   → click "🔥 FWD (burnout)" in BOT32 UI
                            → BOT32 sends mode=1 command on 0x6B1
                            → MITM forces pump to 0% on Haldex bus
                            → front tires spin & warm up
                            → driver clicks "STOCK" to revert when done
                            (or BOT32 auto-revert can be added later)

2. Drive to staging       → click "🅾 STOCK" in BOT32 UI
                            → MITM goes back to pass-through
                            → normal AWD behavior on the road to the line

3. Stage at the line      → click "🚀 5050 (launch)" in BOT32 UI
                            → BOT32 sends mode=2 command
                            → MITM forces pump to 100%
                            → green light → launch with max grip

4. Coast in the traps     → click "🅾 STOCK" when comfortable
                            → MITM back to pass-through
```

The Live State panel shows you exactly what the MITM is doing in real time
(current mode echoed back, actual pump %, speed, pedal).

---

## 🔮 Future enhancements (not yet implemented in BOT32)

- **Steering wheel button trigger** — combines with Feature 1 (Clear DTC
  via MFSW combo, see `docs/future_features.md`). Once MFSW decoder is
  written, a steering combo could trigger LAUNCH or BURNOUT directly
  without needing the laptop.
- **Auto-revert timers** — automatic switch back to STOCK after N seconds
  in BURNOUT/LAUNCH mode (safety). Currently BOT32 only sends the mode;
  it relies on the user or MITM to revert.
- **Speed-based lockout** — disable LAUNCH mode commands above a threshold
  speed (no point at highway speeds, and protects the clutch pack).

These are easy additions once you've validated the basic round-trip with
your MITM module.

---

## 📋 Troubleshooting

| Symptom | Possible cause |
|---|---|
| Haldex live state shows `—` / "no broadcast RX" | MITM module not powered, not connected to the same CAN bus, or `haldex_state_id` mismatch |
| Mode buttons send but MITM doesn't react | `haldex_cmd_id` mismatch with what your MITM module listens for |
| Wrong byte positions in live state | Your MITM module uses a different layout — adjust `DEFAULT_POS_*` constants in `BOT32/haldex_link.cpp` |
| Bus errors appear after enabling | Wrong CAN bus selected (`haldex_bus`), or terminator jumpers misconfigured |

---

## 📜 License reminder

- **BOT32** (this repo) — MIT
- **OpenHaldex-C6** (separate project, not included here) — FASL v1.0
  (Forbes Automotive Source-Available License, personal/non-commercial use)

Always respect the original author's license when using OpenHaldex-C6 hardware
or firmware in your own builds.
