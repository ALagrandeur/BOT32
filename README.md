# BOT32

**BOT32 is the "Main ESP" of a VW Golf MK7 Alltrack 2017.** It is an ESP32 with a
WaveShare 2-CH CAN HAT, wired to the instrument-cluster CAN bus (CAN0) and to the
OBD-II CAN bus (CAN1). It:

- shows **turbo boost on the coolant-temperature needle** of the cluster (lever in S/M);
- reads **live data** (12 values: MAP, ethanol, Haldex, DSG oil, EGT, buttons…);
- serves a **phone web UI** over its own WiFi access point, and a **PC web UI** over USB;
- **clears engine fault codes** with a listen-only procedure (engine ECU only);
- **commands the Haldex AWD modes** (STOCK / FWD / 50-50) of the in-line
  [BOT32-HALDEX](https://github.com/ALagrandeur/BOT32-HALDEX) module over ESP-NOW,
  from the app or with physical button combos.

Successor to [MK7BoostGauge](https://github.com/ALagrandeur/MK7BoostGauge) (Raspberry Pi version, archived due to MCP2515 driver instability).
Logic ported from [mk7-cluster-bench-controller](https://github.com/ALagrandeur/mk7-cluster-bench-controller)
(lineage: mk7-cluster-bench-controller → MK7BoostGauge → BOT32).

## Project map (all repos)

The role descriptions are kept in French, as in the project's shared map.

| Vehicle | Device | Repo / folder | Role |
|---|---|---|---|
| **Golf MK7 Alltrack 2017** | **ESP Main (ESP32 + HAT WaveShare 2-CH CAN)** | **[BOT32](https://github.com/ALagrandeur/BOT32)** | **jauge boost sur le combiné, données live, interface web (cellulaire + PC), effacement des codes moteur, commande des modes Haldex** |
| Golf MK7 Alltrack 2017 | ESP32-CAN-X2 en série sur le bus Haldex | [BOT32-HALDEX](https://github.com/ALagrandeur/BOT32-HALDEX) (privé) | modes Haldex STOCK / FWD / 50-50 |
| Westfalia | ESP32-CAN-X2 | [Remote32](https://github.com/ALagrandeur/Remote32) / Emetteur_CAN_X2 | lit le CAN du Microsquirt et diffuse les données (ESP-NOW + WiFi/UDP) |
| Westfalia | ESP32-P4 écran rond 3.4" | [Remote32](https://github.com/ALagrandeur/Remote32) / Recepteur_P4_IDF | tableau de bord à cadrans (version finale v1.4) |
| Les deux | ESP32-S3 AMOLED 1.8" | [Remote32](https://github.com/ALagrandeur/Remote32) / Recepteur_AMOLED | manette universelle : cadrans Westfalia + mode Golf (modes Haldex) |
| Outil | ESP32 (ESP32RET) + PC | [mk7-cluster-bench-controller](https://github.com/ALagrandeur/mk7-cluster-bench-controller) | banc d'essai du combiné MK7 (dormant) |
| Référence | — | [OpenHaldex-C6-perso](https://github.com/ALagrandeur/OpenHaldex-C6-perso) | copie non modifiée d'OpenHaldex-C6, non utilisée |
| Archivé | — | [MK7BoostGauge](https://github.com/ALagrandeur/MK7BoostGauge) | ancêtre de BOT32 (Raspberry Pi) |

## Status

| | |
|---|---|
| **Current version** | **v4.6.0 — STABLE** (`master`), flashed and confirmed in the car on 2026-10-12 |
| **Previous stable version** | **v4.5.0** (git tag `v4.5.0`) — rollback point |
| **Paired Haldex module** | BOT32-HALDEX **v3.0.0** (stable, all 3 modes confirmed in the car) |

Detailed status, version history and roadmap (French): [ÉTAT_DU_PROJET.md](ÉTAT_DU_PROJET.md).

## Architecture

```
                         VW Golf MK7 Alltrack 2017
   cluster CAN bus ───────┐                         ┌──────── OBD-II port (CAN)
   (18-pin conn. 17/18)   │                         │         (pins 6/14)
                ┌─────────┴─────────────────────────┴─────────┐
                │  BOT32 Main — ESP32 + WaveShare 2-CH CAN HAT │
                │   CAN0 (MCP2515 #0): TX Motor_09 0x647,      │
                │        RX lever / buttons / real coolant     │
                │   CAN1 (MCP2515 #1): UDS polling, Mode 04    │
                │   USB serial (JSON) ── PC web UI (webui/)    │
                │   WiFi AP ch.1 ─────── phone (192.168.4.1)   │
                └──────────────┬───────────────────────────────┘
                               │ ESP-NOW (WiFi channel 1)
                               │ SET_MODE + SET_PASSTHROUGH ▼   STATE ▲
                ┌──────────────┴───────────────┐       ┌──────────────────────────┐
                │ ESP32-CAN-X2 (BOT32-HALDEX)  │◄──────┤ Remote32 AMOLED remote   │
                │ in series on the Haldex bus  │ESP-NOW│ (Golf mode): SET_MODE    │
                │ (private repo)               │       │ only, never arming       │
                └──────────────────────────────┘       └──────────────────────────┘
```

The X2 accepts modes from **two sources**: the Main and the
[Remote32 AMOLED remote](https://github.com/ALagrandeur/Remote32) (Golf mode). Only
the Main can arm/disarm the X2 (SET_PASSTHROUGH). The Main always displays the mode
**reported by the X2**. Protocol details: [docs/haldex_integration.md](docs/haldex_integration.md).

## Features

### 1. Boost on the coolant gauge

- Lever in **S/M** → BOT32 transmits `Motor_09 (0x647)` on the cluster bus at
  `tx_rate_hz` (default **30 Hz**). The coolant needle then shows the intake
  manifold pressure (MAP, UDS DID `0x39C0`).
- **Linear** mapping: `map_min_mbar` (default **250 mbar**) → 50 °C (cold end),
  `map_max_mbar` (default **2068 mbar**) → 130 °C (red zone). Both are settings.
- Lever in **P/R/N/D** → nothing is transmitted on the cluster bus; the cluster shows the
  real coolant temperature. (With the default settings, UDS polling on the OBD bus keeps
  running in every lever position, so live data stays live.)
- If no fresh MAP is available, the needle falls back to the cold end.

Coolant byte formula (validated in the sister project):
`temp_C = byte × 0.7339 − 43.94` → `0x80` = 50 °C, `0xED` = 130 °C. There is no
dead-zone handling: the needle simply follows the linear mapping (the dead-zone mode
was removed in v2.0).

### 2. Live data (12 cells, phone + PC)

| Cell | Source |
|---|---|
| Lever | `0x394` WBA_03 (cluster bus) |
| MAP (mbar) | UDS `0x39C0`, engine `0x7E0`→`0x7E8` |
| Coolant TX (what BOT32 sends / would send) | computed from MAP |
| Real coolant | `0x647` sniffed on the cluster bus |
| Ethanol % | UDS `0xF452`, engine |
| Haldex blockage % | UDS `0x2BF3`, Haldex `0x70F`→`0x779` |
| DSG oil °C | UDS `0x2104`, DSG `0x7E1`→`0x7E9` |
| EGT °C | UDS `0x40D5`, engine |
| Hand brake | `0x30B` byte 2 bit 7 |
| OK button (steering wheel) | `0x5BF` byte 0 |
| Hazard | `0x366` byte 2 bit 4 |
| Traction Control | `0x0FD` byte 6 |

UDS polling: 6 round-robin slots (MAP, ethanol, Haldex blockage, DSG oil, EGT, Haldex
clutch temp `0x2BF1`), `obd2_poll_hz` = **30 Hz in total** (~5 Hz per slot). The button
sniffers are passive (read-only). A Haldex block (mode, link, speed, pedal, lock target,
pump %, clutch temp — display only) is shown below the grid.

### 3. Phone web UI (WiFi access point)

| | |
|---|---|
| Enable | **OFF by default.** Turn it on from the PC UI (card *📶 WiFi remote*) or over serial: `{"cmd":"set","key":"wifi_enabled","value":true}` — starts/stops live, no reboot |
| SSID / password (firmware defaults) | **`BOT32`** / **`BOT32-2026`** (WPA2, 8 characters min) — **change them** (PC UI or phone settings page): the web UI itself has no login, so anyone connected to the access point can read and change every setting |
| Address | **http://192.168.4.1** (channel 1, shared with ESP-NOW) |
| `/` | 12 live cells, Haldex block, **STOCK / FWD / 50-50** buttons (confirmation asked for FWD and 50-50), 🟢/🔴 armed badge, combo cheat-sheet (*Aide-mémoire*) |
| `/settings` | MAP min/max, Poll Hz, TX rate, TX enabled, Force TX, **Effacer les codes moteur**, WiFi AP (SSID/password), Haldex link + X2 peer MAC, **Arm / Disarm the X2** |

HTTP API (same settings dispatcher as the serial `set` command):
`GET /api/status`, `GET /api/get_settings`, `POST /api/set {key, value}`,
`POST /api/cmd {cmd: clear_engine_fault | set_haldex_mode (mode 0..2) | set_haldex_passthrough (on)}`.

The USB serial link stays fully active in parallel with WiFi.
Screenshots (taken on v4.4.0): [docs/screenshots/](docs/screenshots/).

### 4. PC web UI (USB)

Python Flask + Socket.IO app in [`webui/`](webui/) — see **[webui/README.md](webui/README.md)**.
Live grid, CAN frame monitor, mapping / IDs / rates, bench mode + "lamp killer",
Haldex card (via ESP-NOW, or a direct USB-C link to the X2 for development),
WiFi remote, engine-code clearing, combo cheat-sheet. You can unplug USB at any time:
BOT32 keeps running in the car with the last saved settings (NVS).

### 5. Clear engine codes (v4.6.0)

Button *🔧 Effacer les codes moteur* (phone `/settings` and PC UI), or serial
`{"cmd":"clear_engine_fault"}`. **Engine ECU only:**

1. BOT32 goes **listen-only on both CAN buses** (no cluster override, no OBD polling,
   no bench frames) and sets the Haldex to **STOCK**.
2. 300 ms later, OBD-II Mode 04 `[01 04]` is sent to `0x7E0`.
3. Reply on `0x7E8`: `01 44` = cleared · `7F 04 78` = pending (keeps waiting, 15 s max) ·
   other `7F` = refused (NRC shown) · 2 s without a reply = *Pas de réponse du moteur*.
4. 200 ms later, one more STOCK is sent and transmissions resume. The Haldex stays in STOCK.

The result is shown on the page (`clear_engine_state/msg/seq` in the status).

### 6. Haldex AWD modes (via BOT32-HALDEX)

> ⚠️ **Motorsport — closed course only.** Do not use on public roads.

Requires *Haldex link enabled* (`haldex_enabled`, OFF by default).

| Mode | How to select it |
|---|---|
| **FWD** (front wheels only) | **Hazard ON + TC OFF**, in any order, **hand brake down** — or the 🔥 FWD button |
| **50-50** (max lock) | the **same combo with the hand brake pulled** when it completes — or the 🚀 50/50 button |
| **STOCK** | **turn TC back ON** (exits a combo-selected mode) — or the 🅾 STOCK button (the only exit for a mode chosen in the app) |

- The button states must be fresh (< 2 s). Once a combo mode is held, re-pressing the
  hazards changes nothing while TC stays OFF; hazards and hand brake can then be released.
- FWD and 50-50 only act when the X2 is **ARMED** (passthrough OFF). **Arming is done only
  from the app** (`/settings` → *Armer / Désarmer le X2*, or the PC Haldex card).
  The X2 always boots in STOCK mode (its arming state is kept in its own NVS).
- The **Remote32 AMOLED remote** (Golf mode) can also set STOCK / FWD / 50-50; it cannot arm.
- No timed auto-revert, no pedal threshold.

Full guide: [docs/haldex_integration.md](docs/haldex_integration.md).

## Hardware

**ESP32 DevKit** ("ESP32 Dev Module") + **WaveShare 2-CH CAN HAT** (2× MCP2515 with
**16 MHz** crystals + 2× SIT65HVD230 3.3 V transceivers), wired with 9 Dupont jumpers
(the 7 signals below + 3V3 + GND).
HAT jumper **VIO on 3V3** (critical — no level shifter is used). Photo: [HAT.jpg](HAT.jpg).

Pins (from [`BOT32/config.h`](BOT32/config.h)):

| Signal | ESP32 GPIO | HAT |
|---|---|---|
| SPI SCK (shared, VSPI) | 18 | SCK |
| SPI MISO | 19 | MISO |
| SPI MOSI | 23 | MOSI |
| CAN0 (cluster bus) CS | 5 | CS0 |
| CAN0 INT | 4 | INT0 |
| CAN1 (OBD-II bus) CS | 25 | CS1 |
| CAN1 INT | 26 | INT1 |
| Status LED | 2 | — |
| Power | 3V3 / GND | 3V3 / GND |

Both buses run at **500 kbps**, normal mode. `MCP2515_CLOCK_MHZ` must match the crystal.

Car side (details in [docs/wiring_waveshare_hat.md](docs/wiring_waveshare_hat.md)):

- CAN0 → cluster 18-pin connector, pins **17 (H) / 18 (L)**, T-tap (do not cut).
- CAN1 → OBD-II pins **6 (H) / 14 (L)**, ground on 4/5.
- HAT 120 Ω jumpers: **ON on the bench**, **OFF in the car** (the buses are already terminated).
- Power in the car, as documented: LM2596 5 V buck from OBD-II pin 16, 2 A fuse,
  1N5408 diode (BOM ≈ $27 CAD on top of the HAT). Whether pin 16 is cut with the key
  OFF on this car is **to be confirmed** — the firmware has no sleep mode.

## State machine and LED

| Mode | When | Behaviour |
|---|---|---|
| `BOOT` | first 5 s after power-on (`listen_only_boot`) | no Motor_09 TX on the cluster bus. Not a full listen-only: with the default settings, UDS polling on the OBD bus already runs |
| `SILENT` | lever P / R / N / D | no TX on the cluster bus (UDS polling continues) |
| `BOOST` | lever S / M | Motor_09 override transmitted |
| `BENCH_TEST` | bench mode enabled (PC UI) | emits the full cluster bundle for a cluster on the bench; normal logic suspended. When bench mode is turned off, BOT32 goes back through `BOOT` |
| `SAFE_FAULT` | MCP2515 init failed at boot | no TX |

`tx_enabled = false` (master switch) stops all Motor_09 TX without changing the mode.
`force_tx_always` (diagnostic) also transmits in SILENT.

LED (GPIO 2): when the Haldex link is enabled → **solid = X2 online**, **blinking (400 ms) =
searching**. Otherwise it shows the mode: solid = BOOT, slow blink = SILENT, fast blink =
BOOST, medium = BENCH_TEST, very fast = SAFE_FAULT.

## Build and flash (Arduino IDE 2.x)

1. **Board support**: *File → Preferences → Additional Boards Manager URLs*:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`,
   then *Boards Manager* → **esp32 by Espressif Systems** (core 3.x). The exact minimum
   core version is **to be confirmed**: the ESP-NOW send callback uses `wifi_tx_info_t`
   (`BOT32/haldex_espnow.cpp`), which only exists in recent cores (probably 3.3 or newer,
   based on ESP-IDF 5.5 — to be confirmed).
2. **Libraries** (*Tools → Manage Libraries*):

   | Library | Author | Version |
   |---|---|---|
   | **ACAN2515** | Pierre Molinaro | ≥ 2.1.x |
   | **ArduinoJson** | Benoit Blanchon | **7.x** (v6 does not compile: `JsonDocument` API) |

   WiFi, WebServer, ESP-NOW, Preferences and SPI come with the core.
3. Open **`BOT32/BOT32.ino`** (14 `.cpp/.h` modules + `config.h`).
4. *Tools*: **Board = ESP32 Dev Module**, **Flash Size = 4MB**,
   **Partition Scheme = Default 4MB with spiffs**, **Upload Speed = 921600**, your COM port.
5. *Sketch → Upload*.

If the IDE keeps compiling a file that no longer exists, close the IDE and delete the
Arduino sketch build cache (see [CLAUDE.md](CLAUDE.md) §10).

### Expected serial output (115200 baud)

```
================================
  BOT32 - boost gauge override
================================
[NVS] Settings loaded from flash
[CAN] cluster MCP2515 started at 500 kbps (16 MHz xtal)
[CAN] obd2 MCP2515 started at 500 kbps (16 MHz xtal)
[espnow] init OK, peer=FF:FF:FF:FF:FF:FF, ACTUAL channel=1, PS=OFF, my MAC=...
[wifi_ui] ...                      (only if the WiFi AP is enabled)
{"evt":"boot","version":"4.6.0","build":"..."}
{"evt":"settings", ...}
[main] Entering BOOT
[main] BOOT complete -> SILENT (listening)
{"evt":"status", ...}              (every 500 ms)
```

On the very first boot (or after a settings-version change) the NVS line reads
`[NVS] No saved settings ... using defaults` instead.

### Troubleshooting

| Symptom | Check |
|---|---|
| `[CAN] ... begin FAILED` / mode `SAFE_FAULT` | HAT `PWR` LED lit, **VIO jumper on 3V3**, the 9 Dupont wires (SPI, CS, INT, 3V3, GND) |
| "MCP2515 started" but `rx = 0`, `tx_fail` high | crystal mismatch: `MCP2515_CLOCK_MHZ` (16 for the WaveShare HAT); on the bench, 120 Ω jumpers ON |
| Needle does not move | lever in S/M? `tx_enabled` ON? MAP cell updating? |
| Phone can't find the network | the WiFi AP is OFF by default — enable it from the PC UI or over serial |
| Haldex buttons refused / no effect | `haldex_enabled` OFF; X2 not armed (badge 🟢 *Pas armé*); X2 offline (LED blinking) — see [docs/haldex_integration.md](docs/haldex_integration.md) |

## Serial protocol (USB, 115200, one JSON object per line)

- Commands: `ping`, `get_status`, `get_settings`, `factory_reset`, `subscribe_frames`,
  `set {key, value}`, `set_haldex_mode {mode}`, `set_haldex_passthrough {on}`,
  `clear_engine_fault`, `get_bench_frames`, `bench_frame {idx, on}`, `bench_frames_all_off`,
  `clear_all_dtcs` (serial only, no UI button: UDS clear on 14 ECUs, including `0x715`,
  possibly the airbag — to be confirmed; whether to keep it is undecided, see
  [ÉTAT_DU_PROJET.md](ÉTAT_DU_PROJET.md)).
- Events: `boot`, `settings`, `status` (every 500 ms), `ack`, `frame`, `pong`, `log`, `bench_frames`.

## Safety

- **Airbag IDs `0x040` / `0x572`** are blocked in `can_handler.cpp` while `block_airbag` is
  true. `block_airbag` is **forced back to true at every boot** and has no UI checkbox (it
  can still be changed at runtime over serial / `/api/set` until the next reboot).
- **5 s without cluster-bus TX** after power-on (`BOOT`). This does not gate the OBD bus:
  with the default settings, UDS polling starts right away.
- In the car BOT32 only transmits `Motor_09 (0x647)` on the cluster bus (the gateway
  already sends everything else) plus UDS requests on the OBD bus.
- Engine-code clearing targets the **engine ECU only**, with BOT32 silent on both buses.
- Haldex: the X2 boots in STOCK mode; FWD / 50-50 need explicit arming from the app.

## In-vehicle vs bench

In the car, never enable bench mode: its bundle (wake, engine context, heartbeats) would
collide with the real gateway. On the bench, `BENCH_TEST` mode (PC UI) drives a standalone
cluster with RPM / MAP / speed sliders and the "lamp killer" frame injector (HAT 120 Ω
jumpers ON). The original bench tool is
[mk7-cluster-bench-controller](https://github.com/ALagrandeur/mk7-cluster-bench-controller).

## Repository contents

| Path | What it is | State |
|---|---|---|
| `BOT32/` | Firmware sketch (`BOT32.ino` + 14 modules + `config.h`) | current |
| `webui/` | PC web UI — [webui/README.md](webui/README.md) | current |
| [ÉTAT_DU_PROJET.md](ÉTAT_DU_PROJET.md) | Project status, history, roadmap (French) | current |
| [CLAUDE.md](CLAUDE.md) | Rules and conventions for Claude Code (French) | current |
| [docs/haldex_integration.md](docs/haldex_integration.md) | Haldex link guide (ESP-NOW protocol, modes, arming) | current |
| [docs/wiring_waveshare_hat.md](docs/wiring_waveshare_hat.md) | HAT ↔ ESP32 ↔ car wiring (French) | current |
| [docs/screenshots/](docs/screenshots/) | Phone UI screenshots (v4.4.0) | slightly outdated |
| `tools/generate_bench_frames_cpp.py` | Generates `BOT32/bench_frames.cpp` (hard-coded Windows paths; its source capture is not in the repo) | current |
| `tools/verify_crc.py` | One-off CRC check against the sister project | current |
| `HAT.jpg`, `ESP32.jpg` | Hardware photos | current |
| [docs/wiring.md](docs/wiring.md) | Abandoned "Option A" wiring (TWAI + level shifter) | **legacy** |
| [docs/hardware.md](docs/hardware.md) | Early hardware options analysis | **legacy** |
| [docs/future_features.md](docs/future_features.md) | Original design notes of two features since shipped | **legacy** |
| [docs/savvycan_capture_guide.md](docs/savvycan_capture_guide.md), `docs/vw_mqb_powertrain.dbc` | Pre-MITM Haldex research guide | **legacy** |
| [docs/preflight_checklist.md](docs/preflight_checklist.md) | v0.1 first-flash checklist | **legacy** |
| `tools/derive_bench_frames.py` | Older bench-frame helper, replaced by `generate_bench_frames_cpp.py` | **legacy** |
| `Cluster Bench.png` | Screenshot of the mk7-cluster-bench-controller UI | **legacy** |
| `DATA input from CAR/` | Development captures and notes (`caf.csv` is referenced in the code) | **legacy** |

## Acknowledgments

The MQB Haldex protocol knowledge used in this project (protocol facts such as CAN IDs
and mode numbering) was openly reverse-engineered by
[**OpenHaldex-C6** by **Forbes Automotive**](https://github.com/Forbes-Automotive/OpenHaldex-C6).

In this project, the Haldex bus man-in-the-middle is done by
[BOT32-HALDEX](https://github.com/ALagrandeur/BOT32-HALDEX) (private) on an Autosport Labs
**ESP32-CAN-X2**. BOT32 only talks to it over its own ESP-NOW protocol (see
[docs/haldex_integration.md](docs/haldex_integration.md)). Both are freshly written code
(MIT) that uses protocol facts only: **no OpenHaldex-C6 source code, PCB designs or
compiled binaries are included**. OpenHaldex-C6 is licensed under FASL v1.0 — respect
its license.

**Huge thanks to Forbes Automotive** for the open reverse-engineering work on the VW MQB
Haldex platform.

Other credits:
- [commaai/opendbc](https://github.com/commaai/opendbc) — VW MQB CAN message definitions
- [commaai/openpilot](https://github.com/commaai/openpilot) — MQB CRC8H2F algorithm
- [r00li/CarCluster](https://github.com/r00li/CarCluster) — Motor_09 magic bytes
- [ACAN2515](https://github.com/pierremolinaro/acan2515) by Pierre Molinaro
- [ArduinoJson](https://arduinojson.org/) by Benoit Blanchon

## License

MIT — see [LICENSE](LICENSE).
