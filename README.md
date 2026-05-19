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
[CAN] TWAI started (cluster, 500 kbps)
[CAN] MCP2515 started (OBD2, 500 kbps, 8 MHz xtal)
[main] Entering BOOT
{"evt":"boot","version":"0.1","build":"..."}
{"evt":"settings", ...}
```

If MCP2515 init fails, check:
- 5V supply to MCP2515 module
- SPI wiring + level shifter
- Crystal frequency in `BOT32/config.h` (set to 8 or 16 MHz depending on module)

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

🟡 **Early development** — core firmware structure complete, hardware bring-up pending.

| Component | Status |
|---|---|
| MQB CRC8H2F + constants | ✅ ported from sister Python project |
| Coolant byte ↔ temp formula + dead zone skip | ✅ ported |
| CAN handler (TWAI + MCP2515 unified API) | ✅ written |
| OBD2 UDS query + response parsing | ✅ written |
| WBA_03 lever decoder | ✅ written |
| NVS settings persistence | ✅ written |
| USB serial JSON protocol | ✅ written |
| Main sketch + state machine | ✅ written |
| Python web UI | ✅ written |
| Bench hardware test | ⏳ pending hardware delivery |
| Vehicle install + calibration | ⏳ pending bench validation |

## License

MIT — see [LICENSE](LICENSE).

## Credits

- [commaai/opendbc](https://github.com/commaai/opendbc) — VW MQB CAN message definitions
- [commaai/openpilot](https://github.com/commaai/openpilot) — MQB CRC8H2F algorithm
- [r00li/CarCluster](https://github.com/r00li/CarCluster) — Motor_09 magic bytes
- ESP32 [TWAI driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/twai.html) (Espressif)
- [ACAN2515](https://github.com/pierremolinaro/acan2515) by Pierre Molinaro
- [ArduinoJson](https://arduinojson.org/) by Benoit Blanchon
