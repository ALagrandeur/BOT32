#!/usr/bin/env python3
"""
Verify that the C++ CRC implementation in BOT32/vw_mqb.cpp matches the
reference Python implementation from mk7-cluster-bench-controller.

Generates a few test vectors that can be checked against the C++ output.

Usage:
    python3 tools/verify_crc.py

Then in Arduino IDE, add this to setup() temporarily:
    vw_mqb_init();
    uint8_t test[8] = { 0x00, 0x00, 0x03, 0x00, 0, 0, 0, 0 };  // wake template
    for (int ctr = 0; ctr < 16; ctr++) {
        test[0] = 0;
        test[1] = ctr;
        test[0] = mqb_checksum(0x3C0, test, 4);
        Serial.printf("ctr=%d -> CRC=0x%02X\\n", ctr, test[0]);
    }

The values should match the table printed below.
"""

# Generate CRC table
T = [0] * 256
for i in range(256):
    c = i
    for _ in range(8):
        c = ((c << 1) ^ 0x2F) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    T[i] = c

MQB_CONST = {
    0x040: [0x40] * 16,
    0x3C0: [0xC3] * 16,
    0x641: [0x47] * 16,
    0x116: [0xAC] * 16,
    0x32A: [0x29] * 16,
    0x31E: [0x78, 0x68, 0x3A, 0x31, 0x16, 0x08, 0x4F, 0xDE,
            0xF7, 0x35, 0x19, 0xE6, 0x28, 0x2F, 0x59, 0x82],
    0x394: [0x47, 0x94, 0x92, 0x6A, 0x67, 0xB5, 0x0D, 0x38,
            0xE3, 0x8A, 0x5D, 0xB4, 0x54, 0xAB, 0xAE, 0x27],
}


def mqb_checksum(addr, data):
    crc = 0xFF
    for b in data[1:]:
        crc ^= b
        crc = T[crc]
    if addr in MQB_CONST:
        counter = data[1] & 0x0F
        crc ^= MQB_CONST[addr][counter]
        crc = T[crc]
    return crc ^ 0xFF


def apply_crc(addr, payload, counter):
    out = bytearray(payload)
    out[1] = (out[1] & 0xF0) | (counter & 0x0F)
    out[0] = 0
    out[0] = mqb_checksum(addr, bytes(out))
    return bytes(out)


# =============================================================
# Test vectors
# =============================================================
def print_table(addr, template, length):
    print(f"\n=== {addr:#05X} (length={length}) — counter 0..15 ===")
    print(f"{'ctr':>3}  {'payload (hex)':<24}")
    for ctr in range(16):
        p = apply_crc(addr, template[:length], ctr)
        print(f"{ctr:>3}  {p.hex(' ').upper()}")


# Klemmen_Status_01 (wake) — byte 2 = 0x03 (Kl.15+Kl.S)
print_table(0x3C0, bytes([0x00, 0x00, 0x03, 0x00]), 4)

# Motor_Code_01 — engine alive heartbeat
print_table(0x641, bytes([0x00, 0x10, 0x00, 0xE8, 0x03, 0x00, 0x00, 0x00]), 8)

# WBA_03 — gear lever in D (high nibble = 0x40)
print_table(0x394, bytes([0x00, 0x40, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00]), 8)

print()
print("=== If C++ output matches the above, the port is correct. ===")
