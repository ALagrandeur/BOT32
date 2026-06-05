#!/usr/bin/env python3
"""
BOT32 — PC-side web UI server.

Reads JSON lines from the ESP32(s) over USB serial @ 115200, mirrors them to a
SocketIO-connected browser, and sends commands back.

Two serial links:
  - MAIN  : the BOT32 main ESP32 (cluster / boost / bench / OBD / lamp test).
            This is the normal everyday link — unchanged behaviour.
  - X2    : OPTIONAL direct USB-C link to the Haldex MITM module (ESP32-CAN-X2)
            for DEVELOPMENT/diagnostic. When the Haldex source is "usbc", the
            Haldex menu talks straight to the X2 (bypassing ESP-NOW + main).

Haldex routing (haldex_source):
  - "espnow" (default): Haldex commands go to MAIN, which relays over ESP-NOW.
                        The X2 state is whatever MAIN reports in status.haldex.
  - "usbc"            : Haldex commands go directly to the X2 over USB, and the
                        X2's haldex_state is injected into status.haldex for the UI.

Usage:
    pip install -r requirements.txt
    python server.py [--port COM3] [--x2-port COM4]

The selected Haldex source is persisted in config.json (key "haldex_source").
"""
from __future__ import annotations

import argparse
import json
import threading
import time
import webbrowser
from pathlib import Path

import serial
import serial.tools.list_ports
from flask import Flask, send_from_directory, jsonify
from flask_socketio import SocketIO

HERE = Path(__file__).resolve().parent
STATIC = HERE / "static"
CONFIG_PATH = HERE / "config.json"

app = Flask(__name__, static_folder=None)
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

# Global state — current settings (cached from last 'settings' event), latest status, etc.
state = {
    "connected": False,        # MAIN link connected
    "port": None,              # MAIN port name
    "boot": None,
    "settings": None,
    "status": None,
    "frames_recent": [],       # rolling buffer of last N frames
    # --- v3.7.0: direct USB-C dev link to the X2 ---
    "haldex_source": "espnow", # "espnow" (normal) | "usbc" (dev, direct to X2)
    "x2_connected": False,     # X2 USB link connected
    "x2_port": None,           # X2 port name
    "x2_state": None,          # last haldex_state JSON from the X2 (dict)
    "x2_state_ms": 0,          # time.monotonic() of last x2_state (freshness)
}
state_lock = threading.Lock()

ser: serial.Serial | None = None       # MAIN
ser_lock = threading.Lock()
x2_ser: serial.Serial | None = None    # X2 (dev)
x2_lock = threading.Lock()

MAX_RECENT_FRAMES = 100
X2_STALE_S = 1.5   # x2_state older than this => X2 considered offline


# ============================================================
#  config.json (persist the dev Haldex source)
# ============================================================

def load_config_source():
    try:
        cfg = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        src = cfg.get("haldex_source")
        if src in ("espnow", "usbc"):
            with state_lock:
                state["haldex_source"] = src
            print(f"[config] haldex_source restored = {src}")
    except Exception:
        pass  # no config / unreadable -> keep default "espnow"


def save_config_source(src: str):
    try:
        cfg = {}
        if CONFIG_PATH.exists():
            try:
                cfg = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
            except Exception:
                cfg = {}
        cfg["haldex_source"] = src
        CONFIG_PATH.write_text(json.dumps(cfg, indent=2), encoding="utf-8")
    except Exception as e:
        print(f"[config] save failed: {e}")


# ============================================================
#  Serial connection / auto-detect
# ============================================================

def auto_detect_port(exclude: str | None = None) -> str | None:
    """Find a likely ESP32 USB-serial port (optionally excluding one device)."""
    for p in serial.tools.list_ports.comports():
        if exclude and p.device == exclude:
            continue
        desc = (p.description or "").lower()
        manuf = (p.manufacturer or "").lower()
        keywords = ["cp210", "ch340", "ch9102", "ft232", "esp32", "silicon labs", "wch", "usb-serial", "usb serial"]
        if any(k in desc or k in manuf for k in keywords):
            return p.device
    return None


def connect_serial(port: str) -> bool:
    """Connect the MAIN link."""
    global ser
    try:
        s = serial.Serial(port, 115200, timeout=0.1)
        with ser_lock:
            ser = s
        with state_lock:
            state["connected"] = True
            state["port"] = port
        socketio.emit("connection", {"connected": True, "port": port})
        print(f"[serial] MAIN connected to {port}")
        return True
    except Exception as e:
        print(f"[serial] MAIN connect failed on {port}: {e}")
        return False


def disconnect_serial():
    global ser
    with ser_lock:
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass
            ser = None
    with state_lock:
        state["connected"] = False
    socketio.emit("connection", {"connected": False})


def connect_x2(port: str) -> bool:
    """Connect the OPTIONAL direct X2 dev link."""
    global x2_ser
    try:
        s = serial.Serial(port, 115200, timeout=0.1)
        with x2_lock:
            x2_ser = s
        with state_lock:
            state["x2_connected"] = True
            state["x2_port"] = port
        emit_x2_link()
        print(f"[serial] X2 (dev) connected to {port}")
        return True
    except Exception as e:
        print(f"[serial] X2 connect failed on {port}: {e}")
        return False


def disconnect_x2():
    global x2_ser
    with x2_lock:
        if x2_ser is not None:
            try:
                x2_ser.close()
            except Exception:
                pass
            x2_ser = None
    with state_lock:
        state["x2_connected"] = False
        state["x2_state"] = None
    emit_x2_link()


def emit_x2_link():
    with state_lock:
        socketio.emit("x2_link", {
            "source": state["haldex_source"],
            "x2_connected": state["x2_connected"],
            "x2_port": state["x2_port"],
        })


def send_cmd(obj: dict) -> bool:
    """Send a JSON command to the MAIN ESP32."""
    with ser_lock:
        if ser is None:
            return False
        try:
            ser.write((json.dumps(obj) + "\n").encode("utf-8"))
            return True
        except Exception as e:
            print(f"[serial] MAIN write failed: {e}")
            return False


def send_cmd_x2(obj: dict) -> bool:
    """Send a JSON command directly to the X2 (dev link)."""
    with x2_lock:
        if x2_ser is None:
            return False
        try:
            x2_ser.write((json.dumps(obj) + "\n").encode("utf-8"))
            return True
        except Exception as e:
            print(f"[serial] X2 write failed: {e}")
            return False


# Haldex commands that must be routed to the X2 when source == "usbc".
HALDEX_CMDS = {"set_haldex_mode", "set_haldex_passthrough"}


# ============================================================
#  Reader threads
# ============================================================

def _read_lines(s: serial.Serial, buf: bytes):
    """Read available bytes, return (new_buf, [complete_lines])."""
    chunk = s.read(256)
    if not chunk:
        return buf, []
    buf += chunk
    lines = []
    while b"\n" in buf:
        line, _, buf = buf.partition(b"\n")
        line = line.strip()
        if line:
            lines.append(line)
    return buf, lines


def reader_thread():
    """MAIN link reader — unchanged behaviour."""
    buf = b""
    while True:
        time.sleep(0.01)
        with ser_lock:
            s = ser
        if s is None:
            time.sleep(0.5)
            continue
        try:
            buf, lines = _read_lines(s, buf)
        except Exception as e:
            print(f"[serial] MAIN read err: {e}")
            disconnect_serial()
            buf = b""
            continue
        for line in lines:
            try:
                obj = json.loads(line.decode("utf-8", errors="replace"))
            except json.JSONDecodeError:
                socketio.emit("raw", {"line": line.decode("utf-8", errors="replace")})
                continue
            handle_event(obj)


def x2_reader_thread():
    """X2 dev-link reader — collects haldex_state; never touches the MAIN UI feed."""
    buf = b""
    while True:
        time.sleep(0.01)
        with x2_lock:
            s = x2_ser
        if s is None:
            time.sleep(0.5)
            continue
        try:
            buf, lines = _read_lines(s, buf)
        except Exception as e:
            print(f"[serial] X2 read err: {e}")
            disconnect_x2()
            buf = b""
            continue
        for line in lines:
            try:
                obj = json.loads(line.decode("utf-8", errors="replace"))
            except json.JSONDecodeError:
                continue  # human serial logs from the X2 — ignore
            handle_x2_event(obj)


def handle_event(obj: dict):
    """Dispatch a MAIN ESP32 event to web clients (unchanged)."""
    evt = obj.get("evt")
    if not evt:
        return
    if evt == "boot":
        with state_lock:
            state["boot"] = obj
    elif evt == "settings":
        with state_lock:
            state["settings"] = obj
    elif evt == "status":
        # v3.7.0: when in USB-C dev mode, override the haldex block with the
        # X2's own state so the Haldex menu reflects the direct link.
        with state_lock:
            if state["haldex_source"] == "usbc":
                obj["haldex"] = _x2_haldex_block_locked()
            state["status"] = obj
    elif evt == "frame":
        with state_lock:
            state["frames_recent"].append(obj)
            if len(state["frames_recent"]) > MAX_RECENT_FRAMES:
                state["frames_recent"] = state["frames_recent"][-MAX_RECENT_FRAMES:]
    socketio.emit(evt, obj)


def _x2_haldex_block_locked() -> dict:
    """Build a status.haldex dict from the latest X2 state. Caller holds state_lock."""
    st = state.get("x2_state")
    fresh = st is not None and (time.monotonic() - state.get("x2_state_ms", 0) < X2_STALE_S)
    if not fresh:
        return {"online": False, "via": "usbc"}
    return {
        "online": True,
        "via": "usbc",
        "local_mode": st.get("mode", 0),
        "current_mode": st.get("mode", 0),
        "pump_engagement_pct": st.get("pump", 0),
        "lock_target_pct": st.get("target", 0),
        "vehicle_kmh": st.get("kmh", 0),
        "pedal_pct": st.get("pedal", 0),
        "passthrough": st.get("passthrough", 1),
        "car_rx": st.get("car_rx"), "car_txf": st.get("car_txf"),
        "hdx_rx": st.get("hdx_rx"), "hdx_txf": st.get("hdx_txf"),
        "age_ms": int((time.monotonic() - state.get("x2_state_ms", 0)) * 1000),
    }


def handle_x2_event(obj: dict):
    """Dispatch an event from the direct X2 link."""
    evt = obj.get("evt")
    if evt == "boot" and obj.get("device") == "BOT32-HALDEX":
        print(f"[serial] X2 hello: version {obj.get('version')}")
        socketio.emit("x2_hello", obj)
    elif evt == "haldex_state":
        with state_lock:
            state["x2_state"] = obj
            state["x2_state_ms"] = time.monotonic()
        # Only push to the browser when this link is the active Haldex source.
        with state_lock:
            active = state["haldex_source"] == "usbc"
        if active:
            socketio.emit("haldex_state", obj)


# ============================================================
#  HTTP routes
# ============================================================

@app.route("/")
def index():
    return send_from_directory(STATIC, "index.html")


@app.route("/<path:asset>")
def static_file(asset: str):
    return send_from_directory(STATIC, asset)


@app.route("/api/state")
def api_state():
    with state_lock:
        return jsonify(state)


@app.route("/api/ports")
def api_ports():
    return jsonify([
        {"device": p.device, "desc": p.description or ""}
        for p in serial.tools.list_ports.comports()
    ])


# ============================================================
#  SocketIO events from browser
# ============================================================

@socketio.on("connect_serial")
def on_connect_serial(data):
    port = data.get("port")
    if not port:
        port = auto_detect_port(exclude=state.get("x2_port"))
        if not port:
            socketio.emit("connection", {"connected": False, "error": "no port found"})
            return
    if state["connected"]:
        disconnect_serial()
        time.sleep(0.2)
    connect_serial(port)


@socketio.on("disconnect_serial")
def on_disconnect_serial(_):
    disconnect_serial()


@socketio.on("cmd")
def on_cmd(data):
    """Browser → ESP32 command. Haldex cmds route to the X2 when source=usbc."""
    cmd = data.get("cmd")
    with state_lock:
        usbc = state["haldex_source"] == "usbc"
    if usbc and cmd in HALDEX_CMDS:
        if not send_cmd_x2(data):
            socketio.emit("ack", {"evt": "ack", "for": cmd, "ok": False, "msg": "X2 not connected"})
        return
    if not send_cmd(data):
        socketio.emit("ack", {"evt": "ack", "for": cmd, "ok": False, "msg": "not connected"})


@socketio.on("subscribe_frames")
def on_subscribe_frames(data):
    send_cmd({"cmd": "subscribe_frames", "enabled": bool(data.get("enabled"))})


# --- v3.7.0: direct X2 dev-link controls (PC only) ---

@socketio.on("set_haldex_source")
def on_set_haldex_source(data):
    """Switch the Haldex menu between ESP-NOW (normal) and USB-C (direct X2)."""
    src = data.get("source")
    if src not in ("espnow", "usbc"):
        return
    with state_lock:
        state["haldex_source"] = src
    save_config_source(src)
    print(f"[haldex] source -> {src}")
    emit_x2_link()


@socketio.on("connect_x2")
def on_connect_x2(data):
    port = data.get("port")
    if not port:
        # auto: a port that is NOT the MAIN port
        port = auto_detect_port(exclude=state.get("port"))
        if not port:
            socketio.emit("x2_link", {"source": state["haldex_source"],
                                      "x2_connected": False, "error": "no X2 port found"})
            return
    if state["x2_connected"]:
        disconnect_x2()
        time.sleep(0.2)
    connect_x2(port)


@socketio.on("disconnect_x2")
def on_disconnect_x2(_):
    disconnect_x2()


# ============================================================
#  Entry point
# ============================================================

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="MAIN serial port (auto-detected if omitted)")
    ap.add_argument("--x2-port", help="Direct X2 dev serial port (optional)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--http-port", type=int, default=5000)
    ap.add_argument("--no-browser", action="store_true",
                    help="Don't auto-open the browser (default: opens after 1.5s)")
    args = ap.parse_args()

    load_config_source()

    # Reader threads (always running, auto-reconnect)
    threading.Thread(target=reader_thread, daemon=True).start()
    threading.Thread(target=x2_reader_thread, daemon=True).start()

    # Auto-connect MAIN
    port = args.port or auto_detect_port(exclude=args.x2_port)
    if port:
        connect_serial(port)
    else:
        print("[serial] no MAIN port specified or detected — connect via UI later")

    # Auto-connect X2 only if explicitly requested
    if args.x2_port:
        connect_x2(args.x2_port)

    if not args.no_browser:
        url = f"http://{args.host}:{args.http_port}"
        def open_browser():
            time.sleep(1.5)
            print(f"[browser] opening {url}")
            try:
                webbrowser.open(url, new=1)
            except Exception as e:
                print(f"[browser] failed to open: {e}")
        threading.Thread(target=open_browser, daemon=True).start()

    print(f"==> BOT32 web UI on http://{args.host}:{args.http_port}")
    socketio.run(app, host=args.host, port=args.http_port, allow_unsafe_werkzeug=True)


if __name__ == "__main__":
    main()
