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
  // Mapping (v2.0: linear only — dead-zone toggle removed)
  "map_min_mbar", "map_max_mbar",
  // Rates
  "obd2_poll_hz", "tx_rate_hz",
  // v2.4.0: poll_ethanol + poll_haldex_blockage removed from UI (always ON).
  // v2.2: cluster display override
  "cluster_override_enabled",
  "display_trigger_can_id", "display_trigger_byte_idx",
  "display_trigger_rest_value", "display_trigger_pressed_value",
  "display_value_source", "display_override_byte1_high",
  "display_byte3_value_mode",  // v2.3.0: raw / ÷7 / tens / units
  // v2.4.0: clear-engine-fault auto-trigger config (roadmap)
  "cef_trigger_can_id", "cef_trigger_byte_idx",
  "cef_trigger_rest_value", "cef_trigger_pressed_value",
  // Behavior flags (v2.3.3: block_airbag removed from UI — forced ON at boot)
  "tx_enabled", "force_tx_always",
  // Bench test
  "bench_test_enabled", "bench_test_bus", "bench_rpm", "bench_map_mbar",
  "bench_display_value_pct", "bench_force_override",  // v2.2.1
  // Haldex link
  "haldex_enabled", "haldex_bus", "haldex_state_id", "haldex_cmd_id",
  "haldex_transport", "haldex_espnow_peer_mac",
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
  // Update slider displays (sliders need their displayed value updated)
  if (s.bench_rpm !== undefined) $("bench-rpm-display").textContent = s.bench_rpm;
  if (s.bench_map_mbar !== undefined) $("bench-map-display").textContent = s.bench_map_mbar;
  if (s.bench_display_value_pct !== undefined) {
    const dvp = $("bench-dvp-display");
    if (dvp) dvp.textContent = s.bench_display_value_pct;
  }
  // Highlight airbag line in bench list if block_airbag is OFF
  const airbagLine = $("bench-airbag-line");
  if (airbagLine) {
    airbagLine.style.opacity = s.block_airbag ? "0.4" : "1.0";
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
  // v2.3.3: airbag-warning element is hidden — block_airbag is hardcoded ON
  const el = $("airbag-warning");
  if (el) el.style.display = blockEnabled ? "none" : "block";
}

// ===========================================================
//  Auto-save on focus loss (text/number inputs + selects)
// ===========================================================
document.addEventListener("blur", (e) => {
  if (!(e.target && e.target.id && e.target.id.startsWith("set-"))) return;
  if (e.target.type === "checkbox" || e.target.type === "range") return;
  const key = e.target.id.replace("set-", "");
  let value;
  if (isHexInput(e.target)) {
    value = parseHexOrInt(e.target.value);
    if (isNaN(value)) { alert(`Invalid hex value: ${e.target.value}`); return; }
    e.target.value = toHex(value);
  } else {
    value = isNaN(+e.target.value) ? e.target.value : +e.target.value;
  }
  socket.emit("cmd", { cmd: "set", key, value });
}, true);

// Select changes (e.g., bench_test_bus) — save on change
document.addEventListener("change", (e) => {
  if (!(e.target && e.target.id && e.target.id.startsWith("set-"))) return;
  if (e.target.tagName !== "SELECT") return;
  const key = e.target.id.replace("set-", "");
  socket.emit("cmd", { cmd: "set", key, value: +e.target.value });
});

// ===========================================================
//  Sliders — live update display + throttled send (max 10/sec)
// ===========================================================
const sliderSendThrottle = {};
function throttledSliderSend(key, value) {
  // Cancel any pending send for this key, schedule a new one in 100ms
  if (sliderSendThrottle[key]) clearTimeout(sliderSendThrottle[key]);
  sliderSendThrottle[key] = setTimeout(() => {
    socket.emit("cmd", { cmd: "set", key, value });
    sliderSendThrottle[key] = null;
  }, 100);
}

document.querySelectorAll('input[type="range"]').forEach(slider => {
  slider.addEventListener("input", (e) => {
    const key = e.target.id.replace("set-", "");
    const value = +e.target.value;
    // Update visible display immediately
    if (key === "bench_rpm") $("bench-rpm-display").textContent = value;
    if (key === "bench_map_mbar") $("bench-map-display").textContent = value;
    if (key === "bench_display_value_pct") {
      const el = $("bench-dvp-display");
      if (el) el.textContent = value;
    }
    // Send to ESP32 (throttled to 10/sec)
    throttledSliderSend(key, value);
  });
});

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

// v2.3.3: btn-factory-reset removed from UI (no longer needed since
// SETTINGS_VERSION bumps auto-reset on flash). Handler kept null-safe in case
// of dynamic re-add.
const btnFactoryReset = $("btn-factory-reset");
if (btnFactoryReset) {
  btnFactoryReset.addEventListener("click", () => {
    if (!confirm("Reset all settings to defaults?")) return;
    socket.emit("cmd", { cmd: "factory_reset" });
  });
}

// ===========================================================
//  Push ALL settings to ESP32 in one batch (header button)
//  Useful as a "force sync" — settings are auto-pushed on blur/change
//  normally, but this button guarantees the ESP32 has exactly what's
//  shown on screen right now.
// ===========================================================
$("btn-push-all").addEventListener("click", () => {
  const btn = $("btn-push-all");
  // Disable briefly to avoid double-clicks
  btn.disabled = true;

  let count = 0, skipped = 0;
  for (const k of SETTING_KEYS) {
    const el = $("set-" + k);
    if (!el) { skipped++; continue; }

    let value;
    if (el.type === "checkbox") {
      value = el.checked;
    } else if (isHexInput(el)) {
      value = parseHexOrInt(el.value);
      if (isNaN(value)) { skipped++; continue; }
    } else {
      const num = +el.value;
      value = (el.value !== "" && !isNaN(num)) ? num : el.value;
    }
    socket.emit("cmd", { cmd: "set", key: k, value: value });
    count++;
  }

  // Visual feedback
  const orig = btn.innerHTML;
  btn.innerHTML = `✓ Sent ${count} settings`;
  btn.classList.add("success");
  setTimeout(() => {
    btn.innerHTML = orig;
    btn.classList.remove("success");
    btn.disabled = false;
  }, 1800);

  console.log(`[push-all] sent ${count} settings, skipped ${skipped}`);
});

socket.on("settings", (s) => {
  applySettings(s);
  // Haldex: show/hide CAN vs ESP-NOW sub-cards based on transport
  updateHaldexTransportUI(s.haldex_transport || 0);
  // Display BOT32 MAC (read-only, for the user to copy into the MITM ESP32 firmware)
  const macEl = document.getElementById("haldex-my-mac");
  if (macEl && s.bot32_mac) macEl.value = s.bot32_mac;
});

function updateHaldexTransportUI(transport) {
  const canFields = document.querySelector(".transport-can-fields");
  const espnowFields = document.querySelector(".transport-espnow-fields");
  if (canFields)    canFields.style.display    = (transport == 1) ? "none" : "";
  if (espnowFields) espnowFields.style.display = (transport == 1) ? "" : "none";
}

// Also react when user changes the transport dropdown directly
const transportSel = document.getElementById("set-haldex_transport");
if (transportSel) {
  transportSel.addEventListener("change", (e) => {
    updateHaldexTransportUI(+e.target.value);
  });
}

// ===========================================================
//  Live status (NO mode display per user request)
// ===========================================================
socket.on("status", (s) => {
  // v2.4.1: the small text under each live value is now the static CAN address
  // (rendered in HTML). The JS only updates the main value + visual color
  // (active/stale via className).

  // Lever
  $("live-lever").textContent = (s.lever && s.lever !== "?")
    ? (s.gear ? `${s.lever}${s.gear}` : s.lever) : "—";

  // MAP (from OBD2 polling)
  const mapEl = $("live-map");
  if (s.map_mbar >= 0) {
    mapEl.textContent = Math.round(s.map_mbar);
    mapEl.className = "value-big";
  } else {
    mapEl.textContent = "—";
    mapEl.className = "value-big inactive";
  }

  // Coolant override — ALWAYS show the live computed value (v2.0).
  // Greyed when not actively TXing.
  const coolEl = $("live-coolant");
  if (s.coolant_byte !== undefined) {
    const b = s.coolant_byte;
    const temp = (b * 0.7339 - 43.94).toFixed(1);
    coolEl.textContent = `${temp}°C`;
    coolEl.className = s.motor09_tx_active ? "value-big" : "value-big inactive";
  } else {
    coolEl.textContent = "—";
    coolEl.className = "value-big inactive";
  }

  // Real coolant sniffed from cluster Motor_09 bus (CAN0)
  const realEl = $("live-real-coolant");
  if (s.real_coolant_c !== undefined && s.real_coolant_c >= 0) {
    realEl.textContent = `${s.real_coolant_c.toFixed(1)}°C`;
    realEl.className = "value-big";
  } else {
    realEl.textContent = "—";
    realEl.className = "value-big inactive";
  }

  // Bus stats
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

  // Haldex live state (from external MITM module via CAN broadcast)
  updateHaldexLive(s.haldex);

  // v2.1: Ethanol live % (v2.4.1: small text under value is now static CAN address)
  const ethEl = $("live-ethanol");
  if (ethEl) {
    if (s.ethanol_pct !== undefined && s.ethanol_pct >= 0) {
      ethEl.textContent = s.ethanol_pct.toFixed(1) + "%";
      ethEl.className = "value-big";
    } else {
      ethEl.textContent = "—";
      ethEl.className = "value-big inactive";
    }
  }

  // v2.1: Haldex blockage live % (v2.4.1: small text under value is now static CAN address)
  const hbEl = $("live-haldex-blockage");
  if (hbEl) {
    if (s.haldex_blockage_pct !== undefined && s.haldex_blockage_pct >= 0) {
      hbEl.textContent = s.haldex_blockage_pct.toFixed(1) + "%";
      hbEl.className = "value-big";
    } else {
      hbEl.textContent = "—";
      hbEl.className = "value-big inactive";
    }
  }

  // v2.1: Clear-all-DTCs progress indicator
  if (s.clear_all_dtcs_in_progress) {
    const status = $("diag-action-status");
    if (status) {
      status.textContent = "⏳ Clear All DTCs in progress: " +
        s.clear_all_dtcs_progress + "% (current ECU " +
        (s.clear_all_dtcs_ecu || "—") + ")";
      status.style.color = "var(--accent)";
    }
  }

  // v2.2: Cluster override live indicators
  const overPressed = $("live-override-pressed");
  const overValue   = $("live-override-value");
  const overEncoded = $("live-override-encoded");
  if (overPressed) {
    if (s.cluster_override_pressed === true) {
      overPressed.textContent = "✓ PRESSED";
      overPressed.className = "value-big mode-BOOST";
    } else if (s.cluster_override_pressed === false) {
      overPressed.textContent = "released";
      overPressed.className = "value-big inactive";
    }
  }
  if (overValue) {
    if (s.cluster_override_value_pct !== undefined && s.cluster_override_value_pct >= 0) {
      overValue.textContent = s.cluster_override_value_pct.toFixed(1) + "%";
      overValue.className = "value-big";
    } else {
      overValue.textContent = "—";
      overValue.className = "value-big inactive";
    }
  }
  if (overEncoded) {
    if (s.cluster_override_encoded_byte !== undefined) {
      const b = s.cluster_override_encoded_byte;
      overEncoded.textContent = b + " (0x" + b.toString(16).toUpperCase().padStart(2,"0") + ")";
    }
  }
});

function updateHaldexLive(hx) {
  if (!hx) return;
  const modeEl   = $("haldex-live-mode");
  const pumpEl   = $("haldex-live-pump");
  const targetEl = $("haldex-live-target");
  const speedEl  = $("haldex-live-speed");
  const pedalEl  = $("haldex-live-pedal");
  const statusEl = $("haldex-live-status");
  const ageEl    = $("haldex-live-age");
  const rawEl    = $("haldex-live-raw");

  if (!hx.valid) {
    if (modeEl)   { modeEl.textContent = "—";   modeEl.className   = "value-big inactive"; }
    if (pumpEl)   { pumpEl.textContent = "—";   pumpEl.className   = "value-big inactive"; }
    if (targetEl) { targetEl.textContent = "—"; targetEl.className = "value-big inactive"; }
    if (speedEl)  { speedEl.textContent = "—";  speedEl.className  = "value-big inactive"; }
    if (pedalEl)  { pedalEl.textContent = "—";  pedalEl.className  = "value-big inactive"; }
    if (statusEl) { statusEl.textContent = "—"; statusEl.className = "value-big inactive"; }
    if (ageEl)    ageEl.textContent = "no broadcast RX";
    if (rawEl)    rawEl.textContent = "—";
    return;
  }

  if (modeEl)   { modeEl.textContent   = (hx.current_mode_name || "?") + " (" + hx.current_mode + ")"; modeEl.className = "value-big"; }
  if (pumpEl)   { pumpEl.textContent   = hx.pump_engagement_pct + "%"; pumpEl.className = "value-big"; }
  if (targetEl) { targetEl.textContent = hx.lock_target_pct + "%"; targetEl.className = "value-big"; }
  if (speedEl)  { speedEl.textContent  = hx.vehicle_kmh; speedEl.className = "value-big"; }
  if (pedalEl)  { pedalEl.textContent  = hx.pedal_pct + "%"; pedalEl.className = "value-big"; }
  if (statusEl) { statusEl.textContent = "✓ alive"; statusEl.className = "value-big mode-SILENT"; }
  if (ageEl) {
    const sec = (hx.age_ms / 1000).toFixed(1);
    ageEl.textContent = "il y a " + sec + "s";
  }
  if (rawEl && hx.raw) {
    const hex = hx.raw.map(b => b.toString(16).padStart(2, '0').toUpperCase()).join(' ');
    rawEl.textContent = hex;
  }
}

// v2.1: Clear Engine Fault button
const btnClearEng = $("btn-clear-engine-fault");
if (btnClearEng) {
  btnClearEng.addEventListener("click", () => {
    if (!confirm(
      "🔧 Clear Engine Fault\n\n" +
      "Envoyer une trame OBD-II Mode 04 (broadcast 0x700) qui demande à TOUS\n" +
      "les ECU compatibles OBD-II d'effacer leurs DTC d'émission.\n\n" +
      "Continuer ?"
    )) return;
    socket.emit("cmd", { cmd: "clear_engine_fault" });
    const status = $("diag-action-status");
    if (status) {
      status.textContent = "▶ Clear Engine Fault envoyé (Mode 04 broadcast)";
      status.style.color = "var(--accent)";
    }
  });
}

// v2.3.3: Clear All DTCs button removed entirely from UI.

// Haldex mode buttons — send set_haldex_mode command via socketio
document.querySelectorAll('[data-haldex-mode]').forEach(btn => {
  btn.addEventListener('click', (e) => {
    const mode = parseInt(e.currentTarget.dataset.haldexMode, 10);
    if (isNaN(mode)) return;
    socket.emit("cmd", { cmd: "set_haldex_mode", mode: mode });
    // Visual feedback
    const status = $("haldex-cmd-status");
    if (status) {
      const labels = ["STOCK", "FWD (burnout)", "5050 (launch)", "60/40", "75/25", "Expert"];
      status.textContent = "▶ Sent: " + labels[mode] + " (mode " + mode + ")";
      status.style.color = "var(--accent)";
      setTimeout(() => { status.style.color = ""; }, 2000);
    }
  });
});

socket.on("boot", (b) => {
  // v2.4.0: boot info moved to header pill (the in-page boot-info element is hidden)
  const bootInfo = $("boot-info");
  if (bootInfo) bootInfo.textContent = `Booted v${b.version} (${b.build})`;
  const headerVer = $("header-version");
  if (headerVer) {
    headerVer.textContent = `fw: v${b.version}`;
    headerVer.title = `Firmware ESP32 : v${b.version} build ${b.build}`;
    headerVer.className = "pill pill-ok";
  }
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
//  Log — v2.3.3: ESP32 log section removed from UI.
//  Helper kept null-safe; events still consumed silently so they don't
//  trigger unhandled-event warnings in socket.io.
// ===========================================================
const logEl = $("log");
function appendLog(line) {
  if (!logEl) return;
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
