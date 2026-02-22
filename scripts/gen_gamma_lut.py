#!/usr/bin/env python3
"""Generate 256-entry LUT for gamma 2.2 and inverse (degamma) in Verilog format.

Usage:
    python gen_gamma_lut.py          # print both tables
    python gen_gamma_lut.py gamma    # gamma only (linear -> display-encoded)
    python gen_gamma_lut.py degamma  # degamma only (encoded -> linear)
"""
import sys
import math

def gamma_encode_12to8(linear_u12):
    """Linear [0..4095] -> sRGB gamma-encoded [0..255] (12-bit to 8-bit)"""
    x = linear_u12 / 4095.0
    if x <= 0.0:
        return 0
    encoded = math.pow(x, 1.0 / 2.2)
    return min(255, round(encoded * 255))

def gamma_decode_8to12(encoded_u8):
    """sRGB encoded [0..255] -> linear [0..4095] (8-bit to 12-bit)"""
    x = encoded_u8 / 255.0
    linear = math.pow(x, 2.2)
    return min(4095, round(linear * 4095))

def print_lut_v2(name, entries, out_bits, fn):
    print(f"    // {name} LUT ({entries} entries, {out_bits}-bit output)")
    for i in range(entries):
        val = fn(i)
        hex_len = (out_bits + 3) // 4
        print(f"    lut_mem[{i:4d}] = {out_bits}'h{val:0{hex_len}X};")
    print()

mode = sys.argv[1].lower() if len(sys.argv) > 1 else "both"

if mode == "gamma12":
    print("// === Gamma Encoding (12-bit linear -> 8-bit display) ===")
    print_lut_v2("gamma", 4096, 8, gamma_encode_12to8)
elif mode == "degamma12":
    print("// === De-Gamma Decoding (8-bit display -> 12-bit linear) ===")
    print_lut_v2("degamma", 256, 12, gamma_decode_8to12)
else:
    # Legacy 8-bit modes for reference
    def gamma_encode_8(i): return min(255, round(math.pow(i/255.0, 1/2.2)*255))
    def gamma_decode_8(i): return min(255, round(math.pow(i/255.0, 2.2)*255))
    if mode in ("gamma", "both"):
        print_lut_v2("gamma8", 256, 8, gamma_encode_8)
    if mode in ("degamma", "both"):
        print_lut_v2("degamma8", 256, 8, gamma_decode_8)
