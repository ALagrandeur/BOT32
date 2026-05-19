/* BOT32 web UI logic */

const $ = (id) => document.getElementById(id);
const socket = io();

// ===========================================================
//  Helpers — hex/number parsing
// ===========================================================
function parseHexOrInt(s) {
  if (s === undefined || s === null || s === "") return 0;
  s = String(s).trim();
  if (s.startsWith("0x") || s.startsWith("0X")) {
    return parseInt(s.substring(2), 16);
  }
  // If contains a-f or A-F, treat as hex
  if (/[a-fA-F]/.test(s)) {
    return parseInt(s, 16);
  }
  return parseInt(s, 10);
}

function toHex(n, pad = 3) {
  return "0x" + Number(n).toString(16).toUpperCase().padStart(pad, "0");
}

// Distinguish hex inputs (CAN IDs) from regular number inputs
function isHexInput(el) {
  return el.classList.contains("hex-input");
}

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
//  Settings list (key → input id stripped)
// ===========================================================
const SETTING_KEYS = [
  // CAN IDs (hex inputs)
  "cluster_motor09_id", "cluster_wba03_id",
  "obd2_req_id", "obd2_resp_id", "obd2_did_map",
  // Mapping
  "map_min_mbar", "map_max_mbar", "scale", "offset_c",
  // Rates
  "obd2_poll_hz", "tx_rate_hz",
  // Behavior flags
  "tx_enabled", "force_tx_always", "block_airbag",
];

function applySettings(s) {
  for (const k of SETTING_KEYS) {
    const el = $("set-" + k);
    if (!el) continue;
    if (el.type === "checkbox") {
      el.checked = !!s[k];
    } else if (isHexInput(el)) {
      el.value = toHex(s[k] || 0);
    } else {
      el.value = s[k];
    }
  }
  // Update preview labels
  updateFramePreviews(s);
  updateAirbagWarning(s.block_airbag);
}

function updateFramePreviews(s) {
  // OBD2 query preview
  const req = toHex(s.obd2_req_id || 0).replace("0x", "");
  const resp = toHex(s.obd2_resp_id || 0).replace("0x", "");
  const did = (s.obd2_did_map || 0);
  const did_hi = ((did >> 8) & 0xFF).toString(16).toUpperCase().padStart(2, "0");
  const did_lo = (did & 0xFF).toString(16).toUpperCase().padStart(2, "0");

  const reqEl = $("obd2-req-display");      if (reqEl) reqEl.textContent = req;
  const respEl = $("obd2-resp-display");    if (respEl) respEl.textContent = resp;
  const didEl = $("obd2-did-display");      if (didEl)  didEl.textContent = `${did_hi}.${did_lo}`;
  const did2 = $("obd2-did-display2");      if (did2)   did2.textContent = `${did_hi} ${did_lo}`;
  const did3 = $("obd2-did-display3");      if (did3)   did3.textContent = `${did_hi}.${did_lo}`;
  const pollEl = $("obd2-poll-display");
  if (pollEl) pollEl.textContent = Math.round(1000 / (s.obd2_poll_hz || 5));
}

function updateAirbagWarning(blockEnabled) {
  $("airbag-warning").style.display = blockEnabled ? "none" : "block";
}

// ===========================================================
//  Auto-save on focus loss (text/number)
// ===========================================================
document.addEventListener("blur", (e) => {
  if (!(e.target && e.target.id && e.target.id.startsWith("set-"))) return;
  const key = e.target.id.replace("set-", "");
  let value;
  if (e.target.type === "checkbox") {
    value = e.target.checked;
  } else if (isHexInput(e.target)) {
    value = parseHexOrInt(e.target.value);
    if (isNaN(value)) { alert(`Invalid hex value: ${e.target.value}`); return; }
    // Reformat the input to canonical 0xXXX form
    e.target.value = toHex(value);
  } else {
    value = isNaN(+e.target.value) ? e.target.value : +e.target.value;
  }
  socket.emit("cmd", { cmd: "set", key, value });
}, true);

// ===========================================================
//  Checkbox: immediate save + special handling for safety toggles
// ===========================================================
document.addEventListener("change", (e) => {
  if (!(e.target && e.target.id && e.target.id.startsWith("set-"))) return;
  if (e.target.type !== "checkbox") return;
  const key = e.target.id.replace("set-", "");

  // Safety guard for airbag block — double-confirm before disabling
  if (key === "block_airbag" && !e.target.checked) {
    const ok = confirm(
      "⚠ DANGER\n\n" +
      "Disabling Airbag ID block removes a critical safety net.\n" +
      "BOT32 could then transmit on 0x040 / 0x572 — never do this on a real vehicle.\n\n" +
      "Are you SURE you want to disable airbag protection?"
    );
    if (!ok) {
      e.target.checked = true;  // revert
      return;
    }
    const ok2 = confirm("Final confirmation: REALLY disable airbag block ?");
    if (!ok2) {
      e.target.checked = true;
      return;
    }
  }

  // Confirm before disabling TX entirely
  if (key === "tx_enabled" && !e.target.checked) {
    if (!confirm("Disable TX entirely? BOT32 will not transmit anything on cluster CAN.")) {
      e.target.checked = true;
      return;
    }
  }

  socket.emit("cmd", { cmd: "set", key, value: e.target.checked });
  if (key === "block_airbag") {
    updateAirbagWarning(e.target.checked);
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
//  Live status (NO mode display per user request)
// ===========================================================
socket.on("status", (s) => {
  $("live-lever").textContent = (s.lever && s.lever !== "?")
    ? (s.gear ? `${s.lever}${s.gear}` : s.lever) : "—";
  if (s.map_mbar >= 0) {
    $("live-map").textContent = Math.round(s.map_mbar);
    $("live-map-age").textContent = `${(s.map_age_ms/1000).toFixed(1)}s ago`;
  } else {
    $("live-map").textContent = "—";
    $("live-map-age").textContent = "no data";
  }
  if (s.coolant_byte !== undefined) {
    const b = s.coolant_byte;
    const temp = (b * 0.7339 - 43.94).toFixed(1);
    $("live-coolant").textContent = `0x${b.toString(16).toUpperCase().padStart(2,"0")} (${temp}°C)`;
  }
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
socket.on("connection", (c) => {
  if (c.connected) {
    setTimeout(() => socket.emit("cmd", { cmd: "get_settings" }), 200);
    setTimeout(() => socket.emit("cmd", { cmd: "get_status" }), 400);
  }
});
