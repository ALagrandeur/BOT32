# BOT32 — PC web UI (`webui/`)

PC-side companion of the BOT32 main ESP32 (VW Golf MK7 Alltrack 2017). A small
**Python Flask + Socket.IO** server reads the firmware's JSON lines over **USB serial
(115200 baud)**, mirrors them to a browser page and sends your commands back.

It is optional: BOT32 runs on its own in the car with the settings saved in NVS, and
the phone UI (WiFi access point, http://192.168.4.1) works without a PC. See the
[main README](../README.md) for the firmware itself.

## Requirements

- Python 3 (Flask 3.x needs Python 3.8 or newer).
- The packages in [`requirements.txt`](requirements.txt):

  | Package | Version |
  |---|---|
  | flask | ≥ 3.0 |
  | flask-socketio | ≥ 5.3 |
  | pyserial | ≥ 3.5 |
  | simple-websocket | ≥ 1.0 |

- A USB cable to the BOT32 main ESP32 (the board's USB-serial chip, e.g. CP210x / CH340).

## Run it

```bash
cd webui
pip install -r requirements.txt
python server.py                      # auto-detects the main ESP32's port
```

On Windows you can double-click **`run.bat`**: it installs/updates the requirements
quietly, then starts `server.py` (any arguments are passed through).

The browser opens on **http://127.0.0.1:5000** after about 1.5 s.

### Command-line options

| Option | Default | Meaning |
|---|---|---|
| `--port COM3` | auto-detect | Serial port of the **BOT32 main** ESP32 |
| `--x2-port COM4` | none | Serial port of the **ESP32-CAN-X2** Haldex module, for the direct USB-C development link (optional) |
| `--host` | `127.0.0.1` | Address the web server listens on |
| `--http-port` | `5000` | Web server port |
| `--no-browser` | off | Do not open the browser automatically |

Port auto-detection picks the first serial port whose description or manufacturer looks
like an ESP32 USB-serial bridge (CP210x, CH340, CH9102, FT232, WCH, "USB serial"…). With
several boards plugged in, pass `--port` (and `--x2-port`) explicitly, or pick the port in
the page header. If no port is found, the server still starts; connect later from the page.

> **Keep the default `--host 127.0.0.1`.** The page has no login, and the data it relays
> includes the firmware settings (the phone WiFi password among them).

## What the page offers

- **Données live** — the 12 live cells (lever, MAP, coolant TX, real coolant, ethanol,
  Haldex blockage, DSG oil, EGT, hand brake, OK button, hazard, traction control) and a
  **CAN frame monitor** (subscribe on/off to avoid flooding the serial link).
- **MAP → Gauge mapping** — MAP min/max, cluster and OBD CAN IDs, UDS DID, poll and TX
  rates, TX master switch, *Force TX in ALL modes* (diagnostic). Settings are saved to the
  ESP32's NVS when a field loses focus.
- **Bench test mode** — drives a cluster on the bench (RPM / MAP / speed), with the
  "lamp killer" frame injector. Put the HAT 120 Ω jumpers **ON** first; never use it in
  the car.
- **Haldex AWD link** — enable the link, ESP-NOW pairing (BOT32 MAC, optional X2 peer MAC),
  STOCK / FWD / 50-50 buttons, arm/disarm the X2 (passthrough), live Haldex state, and the
  Haldex source selector described below.
- **WiFi remote** — turn the phone access point on/off live, change its SSID/password, see
  its status, URL and number of clients.
- **Effacer les codes moteur** — the listen-only engine-code clearing procedure
  (engine ECU only).
- **Aide-mémoire** — cheat-sheet of the physical button combos.

## Haldex source (`haldex_source`)

The Haldex card can reach the X2 in two ways. The choice is stored in
[`config.json`](config.json) (key `haldex_source`) and restored at the next start.

| Value | Route | Use |
|---|---|---|
| `espnow` (default) | Haldex commands go to the **main** ESP32 over USB; the main relays them to the X2 over ESP-NOW. The Haldex state shown is the one the main reports. | Normal use |
| `usbc` | `set_haldex_mode` / `set_haldex_passthrough` go **directly to the X2** over its own USB-C link (`--x2-port`, or *Connect X2* in the card). The X2's state replaces the Haldex block of the status, including its CAN-health counters (rx / tx-fail / TEC / EFLG). | Development and diagnostics of the X2 firmware |

In `usbc` mode, if the dedicated X2 link is not open, Haldex commands fall back to the main
serial port (useful when the X2 itself was auto-detected as the "main" port). The X2 is
considered offline when its last state is older than 1.5 s. The CAN-health counters exist
**only** in `usbc` mode: the ESP-NOW STATE packet does not carry them.

Switch back to `espnow` for normal use.

## HTTP and Socket.IO interface (for reference)

- HTTP: `GET /` (the page), static files, `GET /api/state` (cached boot / settings /
  status / recent frames / X2 link state), `GET /api/ports` (serial ports).
- Socket.IO from the browser: `connect_serial`, `disconnect_serial`, `cmd` (any firmware
  serial command, forwarded as one JSON line), `subscribe_frames`, `set_haldex_source`,
  `connect_x2`, `disconnect_x2`.
- Socket.IO to the browser: the firmware events (`boot`, `settings`, `status`, `frame`,
  `ack`, `log`…), plus `connection`, `raw` (non-JSON serial lines), `x2_link`,
  `x2_hello`, `haldex_state`.

The firmware's serial commands are listed in the main README (*Serial protocol*).

## Troubleshooting

| Symptom | Check |
|---|---|
| `no MAIN port specified or detected` | USB cable/driver (CP210x / CH340), or pass `--port COMx` |
| `MAIN connect failed on COMx` | Port already in use — close the Arduino IDE Serial Monitor (only one program can open the port) |
| Haldex buttons answer `X2 not connected` | Source is `usbc` but no X2 link is open: connect the X2 or switch back to `espnow` |
| Haldex buttons refused in `espnow` mode | `haldex_enabled` OFF on the main, or X2 offline — see [docs/haldex_integration.md](../docs/haldex_integration.md) |
| Browser does not open | Open http://127.0.0.1:5000 yourself (or you started with `--no-browser`) |
