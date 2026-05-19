/* BOT32 web UI logic */

const $ = (id) => document.getElementById(id);
const socket = io();

// ===========================================================
//  Connection management
// ===========================================================
async function loadPorts() {
  try {
    const r = await fetch("/api/ports");
    const ports = await r.json();
    const sel = $("sel-port");
    sel.innerHTML = '<option value="">(auto-detect)</option>';
    for (const p of ports) {
      const opt = document.createElement("option");
      opt.value = p.device;
      opt.textContent = `${p.device} — ${p.desc}`;
      sel.appendChild(opt);
    }
  } catch (e) {
    console.error("port list:", e);
  }
}

$("btn-refresh-ports").addEventListener("click", loadPorts);
$("btn-connect").addEventListener("click", () => {
  socket.emit("connect_serial", { port: $("sel-port").value || null });
});
$("btn-disconnect").addEventListener("click", () => {
  socket.emit("disconnect_serial", {});
});

socket.on("connection", (data) => {
  const pill = $("conn-pill");
  if (data.connected) {
    pill.textContent = `connected: ${data.port}`;
    pill.className = "pill pill-ok";
  } else {
    pill.textContent = "disconnected";
    pill.className = "pill pill-fail";
  }
});

// ===========================================================
//  Settings (live edit + auto-save on blur)
// ===========================================================
const SETTING_KEYS = [
  "map_min_mbar", "map_max_mbar", "scale", "offset_c",
  "obd2_poll_hz", "tx_rate_hz", "tx_enabled"
];

function applySettings(s) {
  for (const k of SETTING_KEYS) {
    const el = $("set-" + k);
    if (!el) continue;
    if (el.type === "checkbox") {
      el.checked = !!s[k];
    } else {
      el.value = s[k];
    }
  }
}

// Auto-save on focus loss
document.addEventListener("blur", (e) => {
  if (e.target && e.target.id && e.target.id.startsWith("set-")) {
    const key = e.target.id.replace("set-", "");
    const value = e.target.type === "checkbox" ? e.target.checked
                : isNaN(+e.target.value) ? e.target.value : +e.target.value;
    socket.emit("cmd", { cmd: "set", key, value });
  }
}, true);

// Checkbox: send immediately on change
document.addEventListener("change", (e) => {
  if (e.target && e.target.id && e.target.id.startsWith("set-") && e.target.type === "checkbox") {
    const key = e.target.id.replace("set-", "");
    socket.emit("cmd", { cmd: "set", key, value: e.target.checked });
  }
});

$("btn-factory-reset").addEventListener("click", () => {
  if (!confirm("Reset all settings to defaults?")) return;
  socket.emit("cmd", { cmd: "factory_reset" });
});

socket.on("settings", (s) => {
  applySettings(s);
});

// ===========================================================
//  Live status
// ===========================================================
socket.on("status", (s) => {
  // Mode with color coding via class
  const modeEl = $("live-mode");
  modeEl.textContent = s.mode || "—";
  modeEl.className = "value-big mode-" + (s.mode || "unknown");

  $("live-lever").textContent = (s.lever && s.lever !== "?")
    ? (s.gear ? `${s.lever}${s.gear}` : s.lever) : "—";
  if (s.map_mbar >= 0) {
    $("live-map").textContent = Math.round(s.map_mbar);
    $("live-map-age").textContent = `${(s.map_age_ms/1000).toFixed(1)}s ago`;
  } else {
    $("live-map").textContent = "—";
    $("live-map-age").textContent = "no data";
  }
  // Coolant byte (in BOOST mode this is what we send)
  if (s.coolant_byte !== undefined) {
    const b = s.coolant_byte;
    const temp = (b * 0.7339 - 43.94).toFixed(1);
    $("live-coolant").textContent = `0x${b.toString(16).toUpperCase().padStart(2,"0")} (${temp}°C)`;
  }
  // Cluster + OBD2 stats
  if (s.cluster) {
    $("cl-tx-ok").textContent   = s.cluster.tx_ok;
    $("cl-tx-fail").textContent = s.cluster.tx_fail;
    $("cl-rx").textContent      = s.cluster.rx;
    $("cl-err").textContent     = s.cluster.errors;
  }
  if (s.obd2) {
    $("ob-tx-ok").textContent   = s.obd2.tx_ok;
    $("ob-tx-fail").textContent = s.obd2.tx_fail;
    $("ob-rx").textContent      = s.obd2.rx;
    $("ob-err").textContent     = s.obd2.errors;
  }
});

socket.on("boot", (b) => {
  $("boot-info").textContent = `Booted v${b.version} (${b.build})`;
});

// ===========================================================
//  Frame monitor
// ===========================================================
const framesTbody = $("frames-tbody");
let framesEnabled = false;
let framesFilterBus = "";

$("frames-enabled").addEventListener("change", (e) => {
  framesEnabled = e.target.checked;
  socket.emit("subscribe_frames", { enabled: framesEnabled });
});

$("frames-filter-bus").addEventListener("change", (e) => {
  framesFilterBus = e.target.value;
});

$("btn-clear-frames").addEventListener("click", () => {
  framesTbody.innerHTML = "";
});

socket.on("frame", (f) => {
  if (framesFilterBus && f.bus !== framesFilterBus) return;
  const dataHex = f.data.map(b => b.toString(16).padStart(2, '0').toUpperCase()).join(' ');
  const idHex = `0x${f.id.toString(16).toUpperCase().padStart(3, '0')}`;
  const tr = document.createElement("tr");
  tr.innerHTML = `
    <td>${f.ts_ms}</td>
    <td>${f.bus}</td>
    <td class="dir-${f.dir}">${f.dir.toUpperCase()}</td>
    <td class="col-id">${idHex}</td>
    <td>${f.len}</td>
    <td>${dataHex}</td>
  `;
  framesTbody.insertBefore(tr, framesTbody.firstChild);
  // Keep last 200
  while (framesTbody.childElementCount > 200) {
    framesTbody.removeChild(framesTbody.lastChild);
  }
});

// ===========================================================
//  Log
// ===========================================================
const logEl = $("log");
function appendLog(line) {
  const t = new Date().toLocaleTimeString();
  logEl.textContent += `[${t}] ${line}\n`;
  logEl.scrollTop = logEl.scrollHeight;
  // Trim
  const lines = logEl.textContent.split("\n");
  if (lines.length > 500) logEl.textContent = lines.slice(-300).join("\n");
}
socket.on("log", (l) => appendLog(`[${l.level}] ${l.msg}`));
socket.on("raw", (l) => appendLog(`(raw) ${l.line}`));
socket.on("ack", (a) => appendLog(`ack ${a.for} ok=${a.ok}${a.msg ? ' '+a.msg : ''}`));

// ===========================================================
//  Init
// ===========================================================
loadPorts();
// Request settings refresh on connect
socket.on("connection", (c) => {
  if (c.connected) {
    setTimeout(() => socket.emit("cmd", { cmd: "get_settings" }), 200);
    setTimeout(() => socket.emit("cmd", { cmd: "get_status" }), 400);
  }
});
