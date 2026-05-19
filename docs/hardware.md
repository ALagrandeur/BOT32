# BOT32 — Hardware notes

## Status

🟡 **Hardware NOT yet finalized.** This doc captures what's needed and the options being considered.

## Requirements

The ESP32 board needs:

1. **2 independent CAN channels** at 500 kbps:
   - **CAN0** → MK7 cluster bus (8-port connector pins 17/18)
   - **CAN1** → OBD-II port (J1962 pins 6/14)
2. **3.3V logic level** on CAN transceivers (most modern transceivers handle this)
3. **12V input** with regulator to 5V/3.3V (vehicle power)
4. **Optional: WiFi** for diagnostic / OTA updates (ESP32 has it built-in)
5. **Optional: USB-C** for development / firmware flashing

## CAN channel options

### Option A — 1× internal TWAI + 1× MCP2515 over SPI

| Pro | Contre |
|---|---|
| ESP32 standard cheap | Asymmetric (one CAN is faster than the other) |
| Plenty of code/lib | Need separate SPI wiring |
| | MCP2515 known fragile (we just learned the hard way on Pi) |

### Option B — Dedicated dual-CAN ESP32 module

Modules like:
- **CrowPanel ESP32-S3 dual CAN** (commercial)
- **WaveShare ESP32-S3-CAN-FD** (1 CAN FD module)
- **MakerHawk dual CAN shield** for ESP32

| Pro | Contre |
|---|---|
| Symmetric, both CAN identical perf | More expensive |
| Cleaner wiring | Less common |

### Option C — 2× external CAN transceivers on ESP32's TWAI + bit-bang

Not realistic — TWAI is single-instance on ESP32 classic. ESP32-C6 has 2× TWAI but the ecosystem isn't as mature.

### Option D — Use ESP32-S3 + 2 MCP2515 via SPI

Two MCP2515 on separate CS lines. Same SPI bus, different CE pins.

| Pro | Contre |
|---|---|
| Symmetric | MCP2515 + SPI overhead, IRQ management same headaches as Pi |
| Predictable | |

## Recommended path (initial)

**Start with Option A** for prototyping (cheap, off-the-shelf), then migrate to a dedicated module (Option B) if reliability issues arise.

**Suggested initial parts list**:

| Item | Suggested model | ~Price (CAD) |
|---|---|---|
| ESP32 dev board | ESP32-DevKitC-32E (or any clone) | $10 |
| CAN0 transceiver | SN65HVD230 breakout (3.3V) | $5 |
| CAN1 MCP2515 module | MCP2515 CAN BUS Shield (with TJA1050) | $8 |
| 12V → 5V buck | LM2596 module | $3 |
| Connectors | Dupont + JST + auto-OBD2 | $10 |
| **Total** | | **~$35** |

## Pin assignments (tentative)

```
ESP32 GPIO  →  Function
─────────────────────────────────
GPIO 21     →  CAN0 TX (TWAI)
GPIO 22     →  CAN0 RX (TWAI)
GPIO 5      →  CAN1 CS  (MCP2515 SPI chip select)
GPIO 4      →  CAN1 INT (MCP2515 interrupt)
GPIO 18     →  CAN1 SCK (SPI clock)
GPIO 19     →  CAN1 MISO
GPIO 23     →  CAN1 MOSI
GPIO 2      →  Status LED
3.3V        →  Transceiver power (CAN0 SN65HVD230)
5V          →  MCP2515 + TJA1050 power
GND         →  Common ground
```

## Vehicle wiring

### MK7 cluster connector (18-pin)

| Cluster pin | Signal | Connect to |
|---|---|---|
| 1 | Kl.30 (+12V permanent) | Already wired |
| 10 | Kl.31 (GND) | Already wired |
| 16 | Kl.15 (+12V ignition) | Already wired (don't tap) |
| **17** | **CAN-H cluster** | **BOT32 CAN0-H (tap)** |
| **18** | **CAN-L cluster** | **BOT32 CAN0-L (tap)** |

⚠ **Tapping CAN, not breaking**: don't cut the cluster wires. Y-cable or T-tap so the original signal stays intact.

### OBD-II port (J1962)

| OBD pin | Signal | Connect to |
|---|---|---|
| **4** or **5** | GND | BOT32 GND |
| **6** | CAN-H | BOT32 CAN1-H |
| **14** | CAN-L | BOT32 CAN1-L |
| **16** | +12V (battery) | BOT32 power input (via buck) |

OBD-II port supplies always-on +12V on pin 16 — perfect for powering BOT32.

## Termination

- **Cluster bus (CAN0)**: vehicle already has 2× 120Ω terminators (cluster end + gateway end). **DO NOT add another** or you'll mismatch the bus impedance.
- **OBD-II (CAN1)**: same — vehicle has terminators. Don't add ours.

→ **Both CAN transceiver termination jumpers MUST be OFF / disabled** when installed in vehicle.

## Power consumption budget

Should sip current when ignition is OFF (parked, Kl.30 only via tap) :
- If powered from OBD-II pin 16: only on when ignition ON ✓ (auto sleep when key out)
- If powered from cluster Kl.30: would drain battery (need careful sleep mode)

→ **Prefer OBD-II power**. Auto-off when key out.

## Safety policy

- Hardcoded blocklist of forbidden CAN IDs in firmware:
  - `0x040` (Airbag_01)
  - `0x572` (Airbag_02)
- Listen-only at boot for 5 sec to confirm bus is alive before TX
- Auto-revert to SILENT (no TX) if any safety check fails
- Watchdog timer to auto-restart if firmware hangs

---

This doc will be updated as hardware is finalized and tested.
