# BOT32-Haldex — Haldex bus MITM module

Sister sketch to **BOT32 main**. Runs on a **2nd ESP32 + WaveShare 2-CH CAN HAT**
(same hardware as BOT32 main). Sits physically between the PCM and the Haldex
AWD controller on the private Haldex sub-bus, intercepting and optionally
modifying the pump-demand frame for race modes.

Talks to BOT32 main wirelessly via **ESP-NOW** (no chassis CAN connection
required).

---

## ⚠ Important — what this skeleton DOES and DOES NOT do

| Feature | Status |
|---|---|
| Init 2x MCP2515 on shared SPI (WaveShare HAT) | ✅ Works |
| Bridge frames PCM ↔ Haldex (pass-through) | ✅ Works |
| ESP-NOW link with BOT32 main (state + mode commands) | ✅ Works |
| 6-mode state machine + auto-revert timers | ✅ Works |
| **Identify the Haldex demand frame + modify it** | 🔶 **STUB — you fill in** |
| **Extract live pump %, target %, speed, pedal from Haldex frames** | 🔶 **STUB — you fill in** |
| MQB CRC + counter handling for Haldex IDs | 🔶 **STUB — you fill in** |

The skeleton compiles and runs out of the box. In its default state, it acts as
a **transparent bridge** — useful for initial hardware validation. The actual
mode-specific frame modification logic (the "interesting" Haldex-specific part)
lives in `haldex_mitm.cpp` as commented STUB blocks.

---

## ⚖️ Where the Haldex protocol knowledge comes from

The actual Haldex sub-bus reverse-engineering work was done by Forbes Automotive
for their [OpenHaldex-C6](https://github.com/Forbes-Automotive/OpenHaldex-C6)
project — distributed under FASL v1.0 (source-available, personal/non-commercial
use only).

**This BOT32-Haldex skeleton does NOT include any source code, PCB designs, or
binaries from OpenHaldex-C6.** What it provides is the *plumbing* (CAN bridge,
ESP-NOW link, mode state machine) under MIT.

To make this skeleton actually modify the Haldex demand:

1. **Fork OpenHaldex-C6** to your own GitHub account (FASL permits personal use)
2. Study how their code identifies the Haldex demand frame and changes its
   payload for each mode
3. **Reimplement the same logic in your own words** in the STUB blocks of
   `haldex_mitm.cpp` (search for `TODO #1` and `TODO #2` comments)
4. Keep your fork SEPARATE from the BOT32 repo (which stays MIT)

Alternatively, if you reverse-engineer the bus yourself with a CAN analyzer,
fill in the STUBs based on your own findings — no fork needed.

**Do not copy verbatim** (or "rewrite with minor changes") the OpenHaldex-C6
source code into this file. Facts about the protocol (CAN IDs, byte
positions) are not copyrightable and free to use — but the implementation
code is copyrighted under FASL.

---

## 🛒 Hardware

Same as BOT32 main:

| Item | Notes |
|---|---|
| ESP32 DevKit (NodeMCU-32S, ESP32-WROOM-32, etc.) | Any ESP32 with WiFi (required for ESP-NOW) |
| WaveShare 2-CH CAN HAT | 16 MHz crystals, SIT65HVD230 3.3V transceivers |
| 9 Dupont F-F jumpers | HAT GPIO header ↔ ESP32 GPIO |
| LM2596 buck converter 12V→5V | Power from vehicle (Kl.15 switched recommended) |
| Boîtier ABS étanche | Protection in vehicle |
| T-tap connectors | For splicing the Haldex CAN twisted pair |
| Fuse (1A) inline on +12V | Safety |

---

## 🔌 Wiring HAT ↔ ESP32 (identical to BOT32 main)

| HAT (top header) | → | ESP32 GPIO |
|---|---|---|
| 5V | → | VIN |
| GND | → | GND |
| MISO | → | D19 |
| MOSI | → | D23 |
| SCK | → | D18 |
| CS0 | → | D5 |
| CS1 | → | D25 |
| INT0 | → | D4 |
| INT1 | → | D26 |

**HAT VIO jumper must be in `3V3` position** (critical — protects ESP32).

---

## 🔌 Wiring to vehicle (Haldex bus splicing)

1. Locate the private Haldex CAN bus (twisted pair between PCM and Haldex
   AWD controller). Typically under rear seat or near rear differential on MK7
   4Motion. Use a service manual / wiring diagram for your specific car.
2. **Cut** both wires (CAN-H and CAN-L) of the twisted pair.
3. Connect the **PCM-side** halves of the cut to the **CAN0 H/L/G** terminal
   block on the HAT.
4. Connect the **Haldex-side** halves of the cut to the **CAN1 H/L/G** terminal
   block on the HAT.
5. Connect the cable shield (if present) to chassis GND at ONE point only.

### 120Ω termination

**Both** jumpers ON. We replace the OEM terminators that we physically
disconnected when cutting the bus.

| HAT jumper | Position |
|---|---|
| 120R CAN0 (PCM-side) | **ON** (replaces the Haldex-end terminator) |
| 120R CAN1 (Haldex-side) | **ON** (replaces the PCM-end terminator) |

(This is OPPOSITE the BOT32 main rule for cluster/OBD2 — there we set OFF
because the vehicle bus has its own terminators. Here we just cut them off,
so we must replace them on each half of the bus.)

---

## ⚡ Power

For bench testing: power BOT32-Haldex via USB from a laptop.

For vehicle installation:
- +12V from a **Kl.15 (switched) fuse** → 1A inline fuse → 1N5408 diode
  (reverse polarity protection) → LM2596 buck adjusted to **5.0V** → ESP32 VIN
- GND → chassis GND

When the key turns off, Kl.15 cuts power → BOT32-Haldex shuts off cleanly,
no battery drain.

---

## 🎯 Initial testing procedure (BEFORE filling in the STUBs)

This skeleton in its default state lets you validate the hardware safely:

### Step 1 — Bench validation (no vehicle)
1. Wire HAT ↔ ESP32 per the table above (9 Dupont jumpers).
2. VIO jumper on HAT = 3V3.
3. Both 120R jumpers ON.
4. Power via USB from your laptop.
5. Open `BOT32-Haldex/BOT32-Haldex.ino` in Arduino IDE, install **ACAN2515**
   library, set board to ESP32 Dev Module, upload.
6. Open Serial Monitor @ 115200 — you should see:
   ```
   ====================================
     BOT32-Haldex — MITM module
   ====================================
   [CAN] PCM side started @ 500000 bps, xtal 16 MHz
   [CAN] Haldex side started @ 500000 bps, xtal 16 MHz
   [haldex_mitm] STUB init — pass-through mode
   [espnow] init OK, my MAC = AA:BB:CC:DD:EE:FF
   [setup] Ready. Bridging PCM <-> Haldex.
   [5s] mode=STOCK pcm: rx=0 tx_ok=0 ... | hdx: rx=0 ...
   ```
7. **Note the MAC address** — you may want to configure BOT32 main's
   `haldex_espnow_peer_mac` setting to lock to this specific MAC for production.

### Step 2 — ESP-NOW link validation (still on bench)
1. Power on BOT32 main (your existing device).
2. On BOT32 main web UI: enable Haldex link, transport = ESP-NOW.
3. After a few seconds, BOT32 main's UI should show "MITM connection: ✓ alive"
   and the live state grid (mode=STOCK, pump=0%, etc.).
4. Click any mode button in BOT32 main UI (e.g., FWD).
5. On BOT32-Haldex Serial Monitor you should see:
   `[espnow] RX SET_MODE = 1`
   `[mode] STOCK -> FWD`
6. After 10 seconds, auto-revert fires:
   `[mode] auto-revert FWD -> STOCK (after 10000 ms)`

### Step 3 — Vehicle pass-through validation (BEFORE adding mode logic)
1. With STUBs still empty: install in vehicle (cut bus, terminal blocks, power).
2. Turn key on (engine off): the Haldex bus traffic flows through BOT32-Haldex
   unchanged.
3. No fault codes should appear, AWD should behave normally.
4. BOT32 main UI shows live counters increasing (`pcm.rx`, `hdx.rx` both > 0).
5. Drive the car: still normal AWD. STUBs do nothing, so it's pure pass-through.

If steps 1-3 all pass, the hardware + ESP-NOW link + bridge work. Now you can
add the actual mode-specific logic in `haldex_mitm.cpp`.

### Step 4 — Fill in the STUBs (you, with your sources)
- Open `haldex_mitm.cpp`
- Find `TODO #1` — fill in the demand frame modification for each mode
- Find `TODO #2` — fill in the state extraction from Haldex broadcast frames
- Rebuild, reflash, retest on bench, then in vehicle (parked first), then drag
  strip / track day

---

## 📁 File layout

```
BOT32-Haldex/
├── BOT32-Haldex.ino       Main sketch (setup + loop bridge)
├── config.h               Pin assignments, bitrate, timers
├── can_handler.h+cpp      2x MCP2515 wrapper on shared SPI
├── espnow_peer.h+cpp      ESP-NOW MITM side (BOT32 protocol)
├── mode_state.h+cpp       Current mode + auto-revert timer
├── haldex_mitm.h+cpp      ★ STUBS — you fill these in
└── README.md              This file
```

---

## 📜 License

MIT (same as the rest of BOT32).

The skeleton itself is free for any use including commercial. If you fill in
the STUBs using code from OpenHaldex-C6 (FASL), those filled-in functions
are no longer MIT — they inherit FASL restrictions. To avoid license mixing
in a public repo:
- Either keep your filled-in `haldex_mitm.cpp` in a personal/private fork
- Or fill in the STUBs from scratch using only protocol facts (CAN IDs, byte
  layouts) which are not copyrightable

---

## 🙏 Credits

- **Forbes Automotive** for [OpenHaldex-C6](https://github.com/Forbes-Automotive/OpenHaldex-C6) — the open reverse-engineering work on the VW MQB Haldex platform
- **Pierre Molinaro** for the [ACAN2515](https://github.com/pierremolinaro/acan2515) library
- The rest of the BOT32 credits in the main repo README
