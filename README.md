# BOT32

ESP32 firmware for in-vehicle override of the **VW Golf MK7 Alltrack 2017** coolant temperature gauge to display turbocharger boost pressure.

Successor to [MK7BoostGauge](https://github.com/ALagrandeur/MK7BoostGauge) (Raspberry Pi version, archived due to MCP2515 driver instability).
Logic ported from [mk7-cluster-bench-controller](https://github.com/ALagrandeur/mk7-cluster-bench-controller).

## What it does

When the gear lever is in **S/M/N**, BOT32:
1. Polls intake manifold absolute pressure (MAP) via OBD-II UDS query DID `0x39C0` @ 5 Hz
2. Converts MAP value to a fake coolant byte (with cluster dead zone skip)
3. Broadcasts the fake `Motor_09 (0x647)` on the cluster CAN @ 20 Hz
4. Cluster shows the boost pressure on the analog coolant temperature needle

In **P/R/D**, BOT32 stays silent and the cluster displays real engine coolant.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  ESP32 (always-on, autonomous in vehicle)                   │
│                                                             │
│  ┌─── Shared SPI bus (SCK 18, MISO 19, MOSI 23)             │
│  │                                                          │
│  │    ├── MCP2515 #0 (CS 5, INT 4)                          │
│  │    │   └── SIT65HVD230 (3.3V) → MK7 cluster CAN bus      │
│  │    │       ├── RX: WBA_03 (0x394) gear lever             │
│  │    │       └── TX: Motor_09 (0x647) coolant override     │
│  │    │                                                     │
│  │    └── MCP2515 #1 (CS 25, INT 26)                        │
│  │        └── SIT65HVD230 (3.3V) → OBD-II port              │
│  │            ├── TX: UDS query DID 0x39C0 @ 5 Hz           │
│  │            └── RX: UDS response → MAP value              │
│  │                                                          │
│  │    Hardware: WaveShare 2-CH CAN HAT (2x MCP2515 +        │
│  │    2x SIT65HVD230 3.3V transceivers).                    │
│  │    See docs/wiring_waveshare_hat.md.                     │
│  │                                                          │
│  ┌─── USB serial (debug + config when PC connected)         │
│  │    └── Line-delimited JSON protocol                      │
│  │                                                          │
│  └─── NVS (persistent settings: MAP min/max, scale, …)      │
└─────────────────────────────────────────────────────────────┘
```

## State machine

| Lever | Mode | Behavior |
|---|---|---|
| Boot (5s) | BOOT | Listen-only, no TX |
| P, R, D | SILENT | No TX. Cluster shows real engine coolant. |
| S, M, N | BOOST | Poll MAP, TX Motor_09 override. |
| TX disabled by user | SAFE_FAULT | No TX. Failsafe. |

## Hardware

**Réutilisation du HAT WaveShare 2-CH CAN** (déjà en main) : 2× MCP2515 sur SPI partagé + 2× SIT65HVD230 3.3V transceivers + jumpers de terminaison 120Ω + borniers H/L/G à vis.

→ Voir [docs/wiring_waveshare_hat.md](docs/wiring_waveshare_hat.md) pour le pinout HAT↔ESP32 (10 fils Dupont).

Coût hardware additionnel : ~$15 (ESP32 + LM2596 + connecteur OBD2).

> _Note historique : initialement on planifiait Option A (1× TWAI + 1× MCP2515 + level shifter), documentée dans [docs/wiring.md](docs/wiring.md) et [docs/hardware.md](docs/hardware.md). Abandonné car la réutilisation du HAT existant est plus simple et moins chère._

## Firmware setup (Arduino IDE)

### 1. Install Arduino IDE 2.x

[Download](https://www.arduino.cc/en/software).

### 2. Install ESP32 board support

In Arduino IDE:
- **File → Preferences → Additional Boards Manager URLs**, add:
  `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
- **Tools → Board → Boards Manager**, search "esp32" by Espressif Systems, install.
- **Tools → Board → ESP32 Arduino → ESP32 Dev Module** (or matching your dev board).

### 3. Install required libraries

**Tools → Manage Libraries**, install:

| Library | Author | Version |
|---|---|---|
| **ACAN2515** | Pierre Molinaro | >= 2.1.x |
| **ArduinoJson** | Benoit Blanchon | >= 7.x |

### 4. Open the sketch

- Open `BOT32/BOT32.ino` in Arduino IDE
- Select your COM port: **Tools → Port**
- Configure board:
  - **Tools → Board**: ESP32 Dev Module
  - **Tools → Flash Size**: 4 MB
  - **Tools → Partition Scheme**: Default 4 MB
  - **Tools → Upload Speed**: 921600
- **Sketch → Upload**

### 5. Verify in Serial Monitor

Open **Serial Monitor** @ 115200 baud. You should see:

```
================================
  BOT32 - boost gauge override
================================
[NVS] Settings loaded from flash
[CAN] cluster MCP2515 started at 500 kbps (16 MHz xtal)
[CAN] obd2 MCP2515 started at 500 kbps (16 MHz xtal)
[main] Entering BOOT
{"evt":"boot","version":"0.1","build":"..."}
{"evt":"settings", ...}
{"evt":"status", "mode":"BOOT", ...}
```

If MCP2515 init fails (error code in Serial), check:
- HAT power: LED `PWR` on HAT should be lit (orange)
- HAT jumper VIO in position **3V3** (CRITICAL — protects ESP32)
- SPI wiring: 10 Dupont jumpers per [docs/wiring_waveshare_hat.md](docs/wiring_waveshare_hat.md)

If MCP2515 init succeeds BUT no CAN traffic flows (`rx=0`, `tx_fail` high):
- Crystal frequency mismatch — check the silver crystal cans on your HAT.
  WaveShare 2-CH CAN HAT uses **16 MHz** (set in `MCP2515_CLOCK_MHZ` in config.h).
  If your board uses 8 or 20 MHz, update the define.

## PC web UI (USB debug + config)

Optional. Allows live monitoring + parameter tuning.

### 1. Install Python deps

```bash
cd webui
pip install -r requirements.txt
```

### 2. Run the server

```bash
python server.py
```

Or on Windows: double-click `webui/run.bat`.

### 3. Open browser

[http://127.0.0.1:5000](http://127.0.0.1:5000)

Features:
- Auto-detects ESP32 USB port (CP210x / CH340 / etc.)
- Live status (mode, lever, MAP, coolant byte, bus stats)
- Settings editor (auto-saved to NVS on focus loss)
- Live CAN frame monitor (subscribe on/off to avoid clutter)
- ESP32 log mirror

When you **unplug USB**, ESP32 keeps running autonomously in the car with the last-saved settings.

## Coolant mapping (validated formula from sister project)

- `temp_C = byte * 0.7339 - 43.94`
- `0x80` (128) = 50 °C → needle at cold (low boost)
- `0xED` (237) = 130 °C → needle at red zone (high boost)
- Cluster **dead zone** [80, 110] °C: needle damped at center. Mapping skips this zone with a 1 °C safety margin to ensure smooth visible needle movement.

## In-vehicle vs bench

In **vehicle mode** (this project), we only TX `Motor_09 (0x647)`. The real gateway already broadcasts everything else (Wake `0x3C0`, Engine_Code `0x641`, ESP/TSK/EPS heartbeats, Airbag). Duplicating them would cause bus collisions.

For **bench mode** (without a real gateway), see the sister project [mk7-cluster-bench-controller](https://github.com/ALagrandeur/mk7-cluster-bench-controller) which fakes the full engine context bundle.

## Safety

Hardcoded forbidden CAN IDs in `can_handler.cpp` (cannot be transmitted regardless of any code path):
- `0x040` Airbag_01
- `0x572` Airbag_02

5-second listen-only window at boot before any TX. `tx_enabled` master switch in NVS — set to false to fully disable TX without unplugging.

## Status

🟢 **v1.0 — Bench-validated** (2026-05-19). Vehicle install pending.

| Component | Status |
|---|---|
| MQB CRC8H2F + constants | ✅ ported from sister Python project |
| Coolant byte ↔ temp formula + dead zone skip | ✅ ported |
| CAN handler (2× MCP2515 unified API on shared SPI) | ✅ working |
| OBD2 UDS query + response parsing | ✅ written |
| WBA_03 lever decoder | ✅ written |
| NVS settings persistence | ✅ working |
| USB serial JSON protocol | ✅ working |
| Main sketch + state machine | ✅ working |
| Python web UI (auto-open browser) | ✅ working |
| Bench test mode (full sister-project bundle) | ✅ validated on real cluster |
| **Bench cluster test — CAN0** | ✅ **passed (cluster reacts to RPM + MAP sliders)** |
| **Bench cluster test — CAN1** | ✅ **passed (same behavior on second bus)** |
| **Vehicle OBD2 listen-only — MAP via UDS DID 0x39C0** | ✅ **passed 2026-05-23 (MAP reads back live in UI)** |
| **Vehicle lever decode (WBA_03 0x394)** | ✅ **passed 2026-05-23 (lever live in UI)** |
| Vehicle install — Cluster Motor_09 override (full TX) | ⏳ pending coolant byte mapping fix + needle proportionality fix |
| Long-term in-vehicle calibration | ⏳ planned after install |

## Roadmap

Planned features not yet implemented:

- 🔮 **Clear DTC via MFSW button combo** — driver presses combo at steering wheel, BOT32 sends UDS Clear DTC to all ECUs. Needs MFSW frame sniff + UDS ECU list.
- 🔮 **Haldex AWD — Burnout + Launch race modes** (motorsport, closed-circuit) — 3 modes: OFF / BURNOUT (pump 0% = FWD pre-stage tire warm-up) / LAUNCH (pump 100% = 50/50 AWD lock at the line). Auto-revert timers for safety. Needs CAN MITM hardware + Haldex bus reverse-engineering.

See [docs/future_features.md](docs/future_features.md) for full technical
requirements, effort estimates, and safety considerations per feature.

## License

MIT — see [LICENSE](LICENSE).

## Credits

- [commaai/opendbc](https://github.com/commaai/opendbc) — VW MQB CAN message definitions
- [commaai/openpilot](https://github.com/commaai/openpilot) — MQB CRC8H2F algorithm
- [r00li/CarCluster](https://github.com/r00li/CarCluster) — Motor_09 magic bytes
- ESP32 [TWAI driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/twai.html) (Espressif)
- [ACAN2515](https://github.com/pierremolinaro/acan2515) by Pierre Molinaro
- [ArduinoJson](https://arduinojson.org/) by Benoit Blanchon
