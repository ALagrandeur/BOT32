# BOT32 — Future features roadmap

This document tracks features planned for future BOT32 versions but not yet
implemented. Each entry lists what's needed (hardware, reverse-engineering,
code modules) to make the feature work.

---

## 🔮 Feature 1 — Clear DTC via steering wheel button combo

### What it does

The driver presses a combination of buttons on the **multi-function steering
wheel (MFSW)** — for example "OK held 3 sec while pressing DOWN" — and BOT32
automatically broadcasts a **UDS Clear Diagnostic Information** (SID `0x14`)
to all ECUs on the bus. Useful to clear nagging fault codes without an OBD2
scanner.

### Components needed

#### Hardware
- ✅ Already present: BOT32 connected to cluster CAN0 + OBD2 CAN1
- (nothing extra required)

#### Firmware modules to write
| Module | Purpose |
|---|---|
| `mfsw_decoder.h/cpp` | Listen for MFSW frames on CAN0, decode button press/release events |
| `combo_detector.h/cpp` | State machine that detects user-configured button sequences (e.g., "OK + DOWN held 3 sec") |
| `dtc_clear.h/cpp` | UDS Clear DTC sender — broadcast SID 0x14 to multiple ECU addresses |

#### Settings to add (NVS-persisted)
- `clear_dtc_combo` — encoded button sequence (or named preset)
- `clear_dtc_hold_ms` — duration to hold combo before triggering (debounce + safety)
- `clear_dtc_targets` — list of UDS ECU addresses to clear (engine, transmission, ABS, etc.)
- `clear_dtc_enabled` — master enable toggle

#### Web UI additions
- Combo configurator (select up/down/left/right/OK/back + hold duration)
- "Manual trigger" button (clear DTCs now without using MFSW)
- Status display ("Last cleared: X seconds ago — N ECUs responded OK")
- Toggle: enable/disable feature

### Reverse-engineering data we still need

#### A. MFSW frame layout for THIS cluster (5G1 920 740B)
From the sister project (`mk7-cluster-bench-controller/webui/config.json`),
MFSW is on **CAN ID `0x5BF`** (4 bytes) with these payloads:

| Button | Press payload (hex) | Release payload |
|---|---|---|
| UP    | `06 00 01 40` | `00 00 00 40` |
| DOWN  | `06 00 0F 40` | `00 00 00 40` |
| LEFT  | `03 00 01 40` | `00 00 00 40` |
| RIGHT | `02 00 01 40` | `00 00 00 40` |
| OK    | ? (TBD) | `00 00 00 40` |
| BACK  | ? (TBD) | `00 00 00 40` |

→ **TO DO**: connect BOT32 to vehicle (CAN0 already wired), subscribe to
frame monitor in UI, press each MFSW button one at a time, **record the
actual payloads** for OK and BACK. Update this table.

The frame format MIGHT differ slightly from the bench config (we read these
from r00li). Real-vehicle sniff is needed to confirm.

#### B. UDS ECU addresses on MK7 Alltrack 2017
Standard VW MQB ECU addresses (need confirmation per ECU):

| ECU | Request ID | Response ID |
|---|---|---|
| Engine (Motor) | `0x7E0` | `0x7E8` (✅ already used for MAP query) |
| Transmission (DSG) | `0x7E1` | `0x7E9` |
| ABS / ESP | `0x713` | `0x77D` |
| Cluster | `0x714` | `0x77E` |
| Airbag | `0x715` (we should AVOID this) | `0x77F` |
| Steering | `0x712` | `0x77C` |
| HVAC | `0x746` | `0x7AE` |

→ **TO DO**: with BOT32 connected, send a UDS "Tester Present" (SID `0x3E`)
to each candidate address and log which ones respond. That gives us the
real list of present ECUs.

#### C. UDS Clear DTC command format
ISO 14229 standard:
- Request : `[04 14 FF FF FF 00 00 00]`
  - `04` = ISO-TP length (4 data bytes)
  - `14` = SID Clear Diagnostic Information
  - `FF FF FF` = group of DTC to clear (`FF FF FF` = all DTCs)
- Positive response : `[02 54 00 ...]`
  - `54` = SID + 0x40 (positive response)

→ Could test this once with a single ECU before implementing the combo
detector. Safe to send to engine ECU on a parked car.

### Estimated effort
| Task | Effort |
|---|---|
| Sniff MFSW payloads on vehicle | 15 min |
| Identify present UDS ECUs | 30 min |
| Firmware modules (mfsw_decoder + combo_detector + dtc_clear) | 4-6 h |
| Web UI (combo configurator + manual trigger + status) | 2 h |
| Bench test + vehicle validation | 1 h |
| **Total** | **~10 h of work** |

### Safety considerations
- **Combo must be hard to trigger accidentally** (3-5 sec hold of multiple keys)
- **Confirm dialog** before sending if web UI is connected
- **Never send Clear DTC to Airbag ECU** (`0x715`) — could trigger fault codes that ONLY a dealer can clear
- **Log every trigger** for audit (when, which combo, which ECUs)
- Master toggle `clear_dtc_enabled` (default OFF) so feature only activates after explicit user opt-in

---

## 🔮 Feature 2 — Haldex AWD pump duty cycle monitor + control

### What it does

The Haldex (4Motion) AWD controller decides how much torque goes to the
rear wheels by modulating an electro-hydraulic pump from 0% to 100% duty.
- **0% pump** = front wheel drive only (no torque to rear)
- **100% pump** = ~50/50 torque split (max rear engagement)

BOT32 will :
- **Monitor** the live pump % (read from the Haldex controller's broadcasts)
- **Override** it manually via a slider in the web UI (or auto-modulate based
  on user-defined rules: throttle position, lever S/M, etc.)

### Components needed

#### Hardware — this is the BIG one

Haldex CAN bus is a **PRIVATE bus** between the AWD controller and the
PCM/transmission ECU. We can't just T-tap it — we need to **physically break
the bus** and insert BOT32 in the middle so we can:
1. Read what the PCM is asking the Haldex to do
2. Modify/replace those frames before they reach the Haldex

This is called **CAN MITM (man-in-the-middle)** or **CAN gateway/bridge**.

##### Option A — single ESP32 (current BOT32) in passive MITM
- Use 1 ESP32 with HAT (CAN0 = vehicle side, CAN1 = Haldex side)
- Splice the Haldex CAN twisted pair: cut, insert 4 wires (H/L in/out)
- BOT32 forwards every frame from CAN0 → CAN1 and vice versa
- For Haldex demand frames, BOT32 INTERCEPTS and replaces with our value
- ⚠ Risk: if BOT32 reboots, the bus is BROKEN until it boots back up
- ⚠ Risk: timing — must forward frames within microseconds or PCM detects fault

##### Option B — dedicated 2nd ESP32 with CAN bridge firmware
- Keep current BOT32 (cluster + OBD2) untouched
- Add a 2nd ESP32 with its own CAN HAT dedicated to Haldex MITM
- The 2 ESP32s communicate over UART or ESP-NOW for control
- Cleaner separation of concerns
- More expensive (~$40 extra hardware)

##### Option C — passive read + injection (NO MITM)
- BOT32 listens to PCM→Haldex demand frames passively (no bus break)
- BOT32 TXs its own "override demand" frames at higher rate
- Bus arbitration: lower CAN ID wins. If our frame has same ID, collision detected, retries occur
- ⚠ Highly unreliable for override (may not work at all)
- ✅ Works for read-only (live % display)

→ **Recommended path**: start with Option C for READ-ONLY monitoring, validate
the data flow, THEN decide if full control (Option A or B) is worth the risk.

#### Firmware modules to write
| Module | Purpose | Complexity |
|---|---|---|
| `haldex_decoder.h/cpp` | Sniff Haldex demand frames + decode pump % | Low |
| `haldex_override.h/cpp` (Option A/B only) | Intercept PCM frames, modify Haldex demand, retransmit | **High** (timing critical) |
| `can_bridge.h/cpp` (Option A only) | Forward all frames CAN0 ↔ CAN1 with low latency | High (real-time) |

#### Web UI additions
- Live Haldex pump % display (number + bar gauge)
- Manual override slider 0-100%
- Mode selector: "Off (read only)", "Manual override", "Rule-based (throttle, lever, etc.)"
- Override rate limit (don't change too fast — could damage clutch)
- Live graph of pump % over time (last 30 sec)

### Reverse-engineering data we still need

#### A. Find the Haldex CAN bus on MK7 Alltrack 2017
- The Haldex bus on MK7 4Motion is typically a **separate subnet** from the
  main Powertrain CAN
- It connects: PCM → Haldex controller (under rear axle)
- Access points : at the PCM connector OR at the Haldex unit's harness
- **Wiring diagram of MK7 Alltrack 2017 4Motion** required

#### B. Identify Haldex CAN ID + frame format
- Sniff the Haldex bus when connected
- Look for messages from the PCM with predictable patterns (changes with
  throttle / acceleration / cornering)
- Typical Haldex demand frame on VAG VW MQB :
  - Likely ID range : `0x140-0x160` (TSK / drivetrain)
  - One byte typically encodes the demand 0-255 (mapped to 0-100% duty)
  - Other bytes : status flags, CRC, counter
- Need to match against openDBC / Ross-Tech VCDS labels for MQB

#### C. Identify MQB CRC + counter constants for Haldex frames
- If Haldex frames use the MQB CRC (most likely), we need the constants
- Check `mk7-cluster-bench-controller/webui/vw_mqb.py` for any Haldex IDs already known
- Otherwise, need to reverse-engineer from a known-good frame

#### D. Test the override safely
- Bench test : use 2 ESP32s, simulate PCM TX, simulate Haldex RX
- Vehicle test : start with READ-ONLY (Option C)
- Then try override with slow values (10% increments, 1 sec apart) on a closed/private road
- Monitor for fault codes (use Feature 1 for clearing once implemented!)

### Estimated effort
| Task | Effort |
|---|---|
| Locate Haldex bus + reverse engineer frame format | 5-10 h (bench + vehicle sniffing) |
| Hardware MITM setup (cabling, second ESP32 if Option B) | 2-4 h |
| Firmware modules (haldex_decoder + can_bridge OR override) | 10-20 h |
| Web UI (live display + slider + rule engine) | 3-5 h |
| Vehicle test + tuning | 3-5 h |
| **Total** | **~25-45 h of work** |

### Safety considerations — VERY IMPORTANT
- **Haldex is a SAFETY system** — incorrect override could:
  - Trigger AWD fault codes (annoying but recoverable)
  - Cause unexpected traction loss on slippery surface (serious risk)
  - Overheat the Haldex clutch pack if commanded to 100% for too long ($$$$ repair)
- **NEVER test override at high speed first**
- **NEVER use override on public roads until extensively validated**
- **Always provide a hardware kill switch** (relay that bridges the bus
  directly when activated, bypassing BOT32 entirely in case of failure)
- **Bus timeout protection**: if BOT32 fails to forward a frame within
  100ms, the Haldex controller will fault — code must have watchdog + auto-revert
  to pass-through mode

---

## 📋 Implementation priority recommendation

| Feature | Priority | Reason |
|---|---|---|
| Feature 1 (Clear DTC) | **High** — implement first | Useful daily, low risk, builds on existing UDS infrastructure |
| Feature 2 part A: Haldex READ-ONLY monitor | **Medium** | Useful info, low risk, good first step to understand the bus |
| Feature 2 part B: Haldex override | **Low until proven safe** | High risk, requires extensive testing before vehicle use |

---

## 🔄 What you can do NOW to prepare

To unblock these features faster when you're ready :

### For Feature 1
1. **Drive your car** with BOT32 connected (CAN0 + frame monitor in UI, subscribe ON)
2. **Press each MFSW button** one by one, record the CAN frames that appear
3. **Save the recordings** — paste into a new `docs/mfsw_sniff_log.md`
4. **Send UDS Tester Present** to each potential ECU address (manual cansend or via firmware test mode) and log responses

### For Feature 2 (read-only monitor first)
1. **Find a wiring diagram** for MK7 Alltrack 2017 4Motion drivetrain
2. **Identify where the Haldex CAN bus runs** (under rear seat or near rear axle)
3. **Sniff it passively** with a dedicated ESP32 + CAN HAT (or temporarily reuse BOT32)
4. **Compare** known-good frames against:
   - [openDBC vw_mqb.dbc](https://github.com/commaai/opendbc/blob/master/opendbc/dbc/vw_mqb.dbc)
   - VCDS / Ross-Tech labels for "Haldex" / "AWD" / "4Motion"
5. **Document findings** in a new `docs/haldex_can_layout.md`

When the recon work is done, ping me and I'll write the modules.

---

_Document version: 1.0 (2026-05-23) — created when user requested feature planning._
