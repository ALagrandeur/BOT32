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

## 🔮 Feature 2 — Haldex AWD pump duty cycle: race burnout + launch modes

### 🏁 Use case (clarified by user)

**MOTORSPORT — CLOSED-CIRCUIT ONLY** (drag strip / track day, not public roads).

The MK7 Alltrack's Haldex AWD system has a single electro-hydraulic pump that
progressively engages the rear axle:
- **0% pump** = front-wheel drive only (no torque to rear)
- **100% pump** = ~50/50 torque split (max rear axle engagement on Haldex Gen 5)

For drag racing, the driver wants 2 specific modes BOT32 can toggle:

#### 🔥 BURNOUT mode (pre-stage tire warm-up)
- Force pump to **0%** → all torque to front wheels
- Allows spinning the front tires in place to **heat the rubber** before launch
- (Also burns off any debris from the contact patch)
- Typically held for 5-10 seconds before staging

#### 🚀 LAUNCH mode (max-grip standing start)
- Force pump to **100%** → 50/50 torque split engaged
- Maximum mechanical grip from all 4 wheels at launch
- Best 60-foot time, lower wheel spin

#### 🅾 OFF mode (normal driving)
- Passive — Haldex controller does its own thing
- BOT32 does NOT inject Haldex frames
- Used when not staging / between runs

### What BOT32 will do

1. **Monitor** the live pump % the PCM is asking for (always, regardless of mode)
2. **Show it live** in the web UI (gauge + numeric value)
3. **Override** it to 0% or 100% on user demand via 3 mode buttons

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

#### Web UI additions (simplified for race use)
- Live Haldex pump % display (number + bar gauge) — shows what PCM is asking
- **3 large mode buttons** for race control:
  - 🅾 **OFF** (pass-through, default)
  - 🔥 **BURNOUT** (force pump 0%)
  - 🚀 **LAUNCH** (force pump 100%)
- Timer display when in BURNOUT or LAUNCH (auto-revert to OFF after timeout)
- Optional: keyboard shortcut binding (B for burnout, L for launch, Space for off)
- Optional: live graph of pump % over time (last 30 sec) for tuning

#### Safety auto-revert (HIGHLY recommended)
- BURNOUT mode auto-reverts to OFF after **10 sec** (prevents leaving FWD-only
  while driving away from the line)
- LAUNCH mode auto-reverts to OFF after **15 sec** (clutch protection)
- Maximum 30 sec total in any non-OFF mode per "session"
- Watchdog: if web UI disconnects while in burnout/launch, auto-revert to OFF

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

### Safety considerations — MOTORSPORT CONTEXT

**Public roads = absolutely not.** This feature is for **closed-circuit drag/track
only**. Even in a controlled environment, several risks remain:

#### 🔥 Burnout mode (0% pump = FWD-only)
- Risk: if user forgets to switch back to OFF before driving away from the line,
  the car is FWD-only at low traction → potential oversteer in corners
- Mitigation: **10-second auto-revert** to OFF
- Mitigation: cluster icon / loud audio cue when active

#### 🚀 Launch mode (100% pump = 50/50)
- Risk: holding 100% pump while clutch is engaged at high RPM for >30 sec can
  **overheat the Haldex clutch pack** ($1500+ repair on Gen 5 Haldex)
- Risk: simultaneous engagement of front and rear at high slip can cause
  driveline shock (CV joints, prop shaft)
- Mitigation: **15-second auto-revert** to OFF
- Mitigation: lockout above certain speed (e.g., disengage above 60 km/h
  since launch mode is only useful for 0→100 acceleration)

#### General
- **Hardware kill switch mandatory** : relay that physically bridges the
  Haldex CAN H/L back to direct connection, bypassing BOT32 entirely. Wire it
  to a panic button on the dash so the driver can revert in 1 push.
- **Bus timeout watchdog**: if BOT32 fails to forward a frame within 100ms,
  the Haldex controller will fault and disable AWD. Code must have watchdog +
  auto-revert to pass-through.
- **Tester before track day**: extensive bench validation with simulated
  PCM + Haldex on a 2nd ESP32 before any vehicle test.
- **Log every mode change** for post-session debugging.

### Race-day workflow (envisioned UX)

```
1. Stage at burnout box        → driver hits BURNOUT button (or steering combo)
                                 → BOT32 forces pump 0%, timer 10s starts
                                 → front tires spin & heat up
                                 → after 10s, auto-revert to OFF (cluster icon flash)

2. Drive forward to staging    → mode is OFF, normal AWD behavior
                                 → cluster shows real Haldex value

3. Stage at the line           → driver hits LAUNCH button (or steering combo)
                                 → BOT32 forces pump 100%, timer 15s starts
                                 → green light → launch with max grip
                                 → during the run, BOT32 keeps 100% until timer expires
                                   OR until driver hits OFF
                                 → after 15s or speed > 60 km/h, auto-revert to OFF

4. Coast through traps         → mode is OFF, normal AWD for run-out

5. Return to staging           → repeat
```

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
