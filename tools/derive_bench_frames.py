#!/usr/bin/env python3
"""
derive_bench_frames.py — build the bench "lamp killer" frame table from a real
healthy-bus capture, AND derive the per-ID MQB CRC magic constant tables for
the frames that carry an E2E counter+CRC.

Input : a SavvyCAN CSV (OFF->start, lights end OFF).
Output: prints a summary + writes two helper files under analysis/:
   - bench_crc_tables.txt   : C arrays for the new MQB CRC constants
   - bench_frame_rows.txt   : C rows for the candidate-frame table

The MQB checksum (CRC8H2F, poly 0x2F) over bytes[1..7], then XOR a per-ID magic
constant indexed by the 4-bit counter (low nibble of byte1), one more table
step, final XOR 0xFF. We invert that to recover magic[counter] from frames that
already carry a valid CRC in byte0.
"""
import csv, sys, statistics
from collections import defaultdict, Counter

CSV = sys.argv[1] if len(sys.argv) > 1 else \
    r"C:\Users\AntoineLagrandeur\BOT32\DATA input from CAR\OFF to start cluster.csv"
OUTDIR = r"C:\Users\AntoineLagrandeur\MK7 cluster\analysis"

# Build CRC8H2F table (poly 0x2F) + its inverse (it's a bijection).
T = []
for i in range(256):
    c = i
    for _ in range(8):
        c = ((c << 1) ^ 0x2F) & 0xFF if (c & 0x80) else (c << 1) & 0xFF
    T.append(c)
TINV = [0] * 256
for i in range(256):
    TINV[T[i]] = i

# Already sent by the bench, or cluster outputs / button sniffers -> exclude.
EXCLUDE = {0x3C0, 0x641, 0x107, 0x647, 0x040, 0x106, 0x116, 0x65D, 0x31E, 0x32A,
           0x30B, 0x5BF, 0x366}

# Known names (best-effort, for labels)
NAMES = {0x0FD:"ESP_21", 0x101:"ESP_02", 0x147:"Motor?", 0x12B:"ESP/Getr?",
         0x31B:"status?", 0x32F:"status?", 0x394:"WBA_03", 0x395:"?",
         0x391:"?", 0x462:"?", 0x520:"?"}

rows = defaultdict(list)   # id -> list of (ts_us, dlc, [bytes])
with open(CSV, newline="") as f:
    r = csv.reader(f)
    next(r, None)
    for row in r:
        if len(row) < 6: continue
        try:
            ts = int(row[0]); cid = int(row[1], 16); ext = row[2].strip().lower()
            dlc = int(row[5])
        except ValueError:
            continue
        if ext == "true": continue          # skip 29-bit / UDS
        data = []
        for i in range(6, 6 + 8):
            if i < len(row) and row[i].strip() != "":
                try: data.append(int(row[i], 16))
                except ValueError: data.append(0)
        rows[cid].append((ts, dlc, data))

total_us = max(ts for v in rows.values() for ts, _, _ in v) - \
           min(ts for v in rows.values() for ts, _, _ in v)
late_start = max(ts for v in rows.values() for ts, _, _ in v) - total_us // 10

def derive_magic(cid, frames):
    """Return (magic[16] or None, consistent?, coverage)."""
    cand = defaultdict(Counter)
    for ts, dlc, d in frames:
        if len(d) < 8: continue
        ctr = d[1] & 0x0F
        # partial CRC over bytes 1..7
        crc = 0xFF
        for b in d[1:8]:
            crc = T[crc ^ b]
        target = d[0] ^ 0xFF        # = T[partial ^ magic]
        idx = TINV[target]          # partial ^ magic
        magic = idx ^ crc
        cand[ctr][magic] += 1
    magic = [None] * 16
    consistent = True
    for c in range(16):
        if cand[c]:
            top, n = cand[c].most_common(1)[0]
            magic[c] = top
            if len(cand[c]) > 1: consistent = False
    coverage = sum(1 for m in magic if m is not None)
    return magic, consistent, coverage

def looks_counter(frames):
    """Does byte1 low-nibble increment 0->F across consecutive frames?"""
    inc = tot = 0
    prev = None
    for ts, dlc, d in sorted(frames):
        if len(d) < 2: continue
        n = d[1] & 0x0F
        if prev is not None:
            tot += 1
            if (prev + 1) & 0x0F == n: inc += 1
        prev = n
    return tot > 0 and inc / tot > 0.8

crc_tables = []
frame_rows = []
summary = []

for cid in sorted(rows):
    if cid in EXCLUDE: continue
    fr = rows[cid]
    if len(fr) < 50: continue                # genuinely periodic only
    ts = sorted(t for t, _, _ in fr)
    deltas = [ts[i+1]-ts[i] for i in range(len(ts)-1)]
    period_ms = round(statistics.median(deltas)/1000.0, 1) if deltas else 0
    late = [d for t, dl, d in fr if t >= late_start and len(d) >= 1]
    sample = late[-1] if late else fr[-1][2]
    dlc = fr[-1][1]
    has_ctr = looks_counter(fr)
    note = ""
    if has_ctr:
        magic, consistent, cov = derive_magic(cid, fr)
        if cov >= 12 and consistent:
            arr = ", ".join("0x%02X" % (m if m is not None else 0) for m in magic)
            crc_tables.append("static const uint8_t MQB_CONST_0x%03X[16] = { %s };" % (cid, arr))
            note = "CRC derived (cov %d/16%s)" % (cov, "" if consistent else " INCONSISTENT")
        else:
            note = "CRC counter but derive weak (cov %d, consistent=%s)" % (cov, consistent)
    else:
        note = "static"
    payload = ", ".join("0x%02X" % b for b in (sample + [0]*8)[:8])
    nm = NAMES.get(cid, "?")
    frame_rows.append('  { 0x%03X, %3d, %d, {%s}, %s },  // %s %s'
                      % (cid, int(period_ms) if period_ms else 100, dlc, payload,
                         "true" if has_ctr else "false", nm, note))
    summary.append((cid, period_ms, dlc, has_ctr, note, nm))

# Validation: re-derive a KNOWN id (0x3C0 -> all 0xC3) to prove the method.
val = ""
if 0x3C0 in rows:
    m, cons, cov = derive_magic(0x3C0, rows[0x3C0])
    allC3 = all((x == 0xC3) for x in m if x is not None)
    val = "VALIDATION 0x3C0 -> %s (expect all 0xC3): %s" % (
        ["%02X" % (x or 0) for x in m], "PASS" if allC3 else "FAIL")

import os
os.makedirs(OUTDIR, exist_ok=True)
with open(os.path.join(OUTDIR, "bench_crc_tables.txt"), "w") as f:
    f.write("\n".join(crc_tables) + "\n")
with open(os.path.join(OUTDIR, "bench_frame_rows.txt"), "w") as f:
    f.write("\n".join(frame_rows) + "\n")

print(val)
print("candidate periodic frames (excl. already-bench / cluster outputs): %d" % len(summary))
print("CRC tables derived: %d" % len(crc_tables))
print()
print("%-6s %-7s %-3s %-5s %-10s %s" % ("ID","per_ms","dlc","ctr","name","note"))
for cid, per, dlc, ctr, note, nm in summary:
    print("0x%03X  %-7s %-3d %-5s %-10s %s" % (cid, per, dlc, "yes" if ctr else "no", nm, note))
