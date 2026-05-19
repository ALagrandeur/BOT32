#!/usr/bin/env python3
"""
BOT32 — PC-side web UI server.

Reads JSON lines from ESP32 over USB serial @ 115200, mirrors them to a
SocketIO-connected browser. Sends commands back to ESP32 via serial.

Usage:
    pip install -r requirements.txt
    python server.py [--port COM3 | --port /dev/ttyUSB0]

Then open http://127.0.0.1:5000/ in your browser.

If --port is not given, tries to auto-detect the ESP32 (looks for CP210x,
CH340, etc. USB-serial chips).
"""
from __future__ import annotations

import argparse
import json
import threading
import time
from pathlib import Path

import serial
import serial.tools.list_ports
from flask import Flask, send_from_directory, jsonify
from flask_socketio import SocketIO

HERE = Path(__file__).resolve().parent
STATIC = HERE / "static"

app = Flask(__name__, static_folder=None)
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

# Global state — current settings (cached from last 'settings' event), latest status, etc.
state = {
    "connected": False,
    "port": None,
    "boot": None,
    "settings": None,
    "status": None,
    "frames_recent": [],   # rolling buffer of last N frames
}
state_lock = threading.Lock()
ser: serial.Serial | None = None
ser_lock = threading.Lock()

MAX_RECENT_FRAMES = 100


# ============================================================
#  Serial connection / auto-detect
# ============================================================

def auto_detect_port() -> str | None:
    """Find a likely ESP32 USB-serial port."""
    candidates = []
    for p in serial.tools.list_ports.comports():
        desc = (p.description or "").lower()
        manuf = (p.manufacturer or "").lower()
        # Common USB-serial chips found on ESP32 dev boards
        keywords = ["cp210", "ch340", "ch9102", "ft232", "esp32", "silicon labs", "wch"]
        if any(k in desc or k in manuf for k in keywords):
            candidates.append(p.device)
    if candidates:
        return candidates[0]
    return None


def connect_serial(port: str) -> bool:
    global ser
    try:
        s = serial.Serial(port, 115200, timeout=0.1)
        with ser_lock:
            ser = s
        with state_lock:
            state["connected"] = True
            state["port"] = port
        socketio.emit("connection", {"connected": True, "port": port})
        print(f"[serial] connected to {port}")
        return True
    except Exception as e:
        print(f"[serial] connect failed on {port}: {e}")
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


def send_cmd(obj: dict) -> bool:
    """Send a JSON command to ESP32."""
    with ser_lock:
        if ser is None:
            return False
        try:
            line = json.dumps(obj) + "\n"
            ser.write(line.encode("utf-8"))
            return True
        except Exception as e:
            print(f"[serial] write failed: {e}")
            return False


# ============================================================
#  Reader thread — parses incoming JSON lines
# ============================================================

def reader_thread():
    buf = b""
    while True:
        time.sleep(0.01)
        with ser_lock:
            s = ser
        if s is None:
            time.sleep(0.5)
            continue
        try:
            chunk = s.read(256)
        except Exception as e:
            print(f"[serial] read err: {e}")
            disconnect_serial()
            continue
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, _, buf = buf.partition(b"\n")
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line.decode("utf-8", errors="replace"))
            except json.JSONDecodeError:
                # Not JSON — probably a debug print from ESP32 (e.g., during boot)
                socketio.emit("raw", {"line": line.decode("utf-8", errors="replace")})
                continue
            handle_event(obj)


def handle_event(obj: dict):
    """Dispatch an ESP32 event/notification to web clients."""
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
        with state_lock:
            state["status"] = obj
    elif evt == "frame":
        with state_lock:
            state["frames_recent"].append(obj)
            if len(state["frames_recent"]) > MAX_RECENT_FRAMES:
                state["frames_recent"] = state["frames_recent"][-MAX_RECENT_FRAMES:]
    # Forward all events to the browser
    socketio.emit(evt, obj)


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
    """Snapshot of current state — useful for page reloads."""
    with state_lock:
        return jsonify(state)


@app.route("/api/ports")
def api_ports():
    """List available serial ports."""
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
        port = auto_detect_port()
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
    """Browser → ESP32 command passthrough."""
    if not send_cmd(data):
        socketio.emit("ack", {"evt": "ack", "for": data.get("cmd"), "ok": False, "msg": "not connected"})


@socketio.on("subscribe_frames")
def on_subscribe_frames(data):
    enabled = bool(data.get("enabled"))
    send_cmd({"cmd": "subscribe_frames", "enabled": enabled})


# ============================================================
#  Entry point
# ============================================================

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="Serial port (auto-detected if omitted)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--http-port", type=int, default=5000)
    args = ap.parse_args()

    # Start reader thread (always running, auto-reconnects)
    threading.Thread(target=reader_thread, daemon=True).start()

    # Auto-connect at startup if port specified or detected
    port = args.port or auto_detect_port()
    if port:
        connect_serial(port)
    else:
        print("[serial] no port specified or detected — connect via UI later")

    print(f"==> BOT32 web UI on http://{args.host}:{args.http_port}")
    socketio.run(app, host=args.host, port=args.http_port, allow_unsafe_werkzeug=True)


if __name__ == "__main__":
    main()
