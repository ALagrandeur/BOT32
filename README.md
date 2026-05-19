# BOT32

ESP32 firmware for in-vehicle override of the **VW Golf MK7 Alltrack 2017** coolant temperature gauge to display turbocharger boost pressure.

Successor to [MK7BoostGauge](https://github.com/ALagrandeur/MK7BoostGauge) (Raspberry Pi version, archived due to MCP2515 driver instability).
Logic ported from [mk7-cluster-bench-controller](https://github.com/ALagrandeur/mk7-cluster-bench-controller).

## Goal

When the gear lever is in **S/M/N**, override the cluster's coolant temperature gauge to display **manifold absolute pressure (MAP)** in real-time as a boost indicator. In P/R/D, stay silent and let the cluster display real coolant temperature.

## Hardware target

- 1× ESP32 (model TBD — needs 2× CAN or 1× CAN + 1× MCP2515-via-SPI)
- 2× CAN transceivers (e.g., SN65HVD230 for 3.3V, or TJA1050 for 5V)
- 12V → 5V/3.3V buck converter for in-car use
- Connector to vehicle: cluster CAN H/L (pins 17/18 of cluster connector) + OBD-II port (pins 6/14)

Hardware exact model TBD — see [docs/hardware.md](docs/hardware.md).

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                  ESP32 in vehicle                       │
│                                                         │
│   CAN0  ◄── Cluster bus (read WBA_03 lever)            │
│   CAN0  ──► Cluster bus (TX Motor_09 when in BOOST)    │
│                                                         │
│   CAN1  ──► OBD-II port (UDS query DID 0x39C0 for MAP)  │
│   CAN1  ◄── OBD-II port (parse UDS response)            │
│                                                         │
│   Logic:                                                │
│     lever in {S, M, N}  → mode BOOST                    │
│     poll MAP @ 5 Hz, map to coolant byte                │
│     TX Motor_09 (0x647) @ 20 Hz                         │
└─────────────────────────────────────────────────────────┘
```

## Modes

| Lever | Mode | TX behavior |
|---|---|---|
| **P, R, D** | SILENT | No TX. Cluster displays real engine coolant. |
| **S, M, N** | BOOST | TX Motor_09 @ 20 Hz with MAP→coolant byte mapping. |

## Coolant mapping (from sister project)

- Formula: `temp_C = byte * 0.7339 - 43.94`  (validated empirically on 5G1 920 740B cluster)
- Range: byte `0x80` = 50°C, byte `0xED` = 130°C
- **Dead zone**: cluster damping holds needle at 90°C for any real temp in [80, 110]°C. Mapping splits useful range into [50-80] + [110-130] to skip the dead zone.

## Bus context (vehicle mode — IMPORTANT)

Unlike bench mode where we need to fake the entire engine context (Motor_Code_01, ESP heartbeats, Airbag_01, etc.), in **vehicle mode**:

- The real gateway already broadcasts everything (Wake, Engine_Code, ESP/TSK/EPS, Airbag)
- **We must NOT duplicate these** — would cause bus collisions
- We **only** TX `Motor_09 (0x647)` to override the coolant byte
- The cluster sees our Motor_09 instead of the real one (same ID = last writer wins)

This is much simpler than bench mode.

## Safety

- `Airbag_01 (0x040)` and `Airbag_02 (0x572)` **NEVER** transmitted (hardcoded blocklist)
- `Listen-only` mode at boot until lever and MAP signals are confirmed valid
- Auto-fallback to SILENT mode if any safety check fails

## Status

🟡 **Early development** — initial structure and skeleton only. Hardware not yet selected.

## License

MIT. See [LICENSE](LICENSE).

## Credits

- [commaai/opendbc](https://github.com/commaai/opendbc) — VW MQB CAN message definitions
- [commaai/openpilot](https://github.com/commaai/openpilot) — MQB CRC8H2F algorithm
- [r00li/CarCluster](https://github.com/r00li/CarCluster) — Motor_09 magic bytes, MFSW button codes
