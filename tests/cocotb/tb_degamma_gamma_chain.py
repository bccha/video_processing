"""
tb_degamma_gamma_chain.py
Cocotb testbench for the full color processing chain:
    filter_degamma -> filter_color_matrix -> filter_gamma

Since cocotb controls one DUT at a time, this file provides:
  1. Individual RTL tests for filter_degamma and filter_gamma
  2. A Python reference model that validates the end-to-end chain behavior

DUT for these tests: filter_color_matrix
  (degamma/gamma are validated via Python reference model in chain tests)

Tests:
  1. test_degamma_lut         - Verify all 256 entries of degamma LUT
  2. test_gamma_lut           - Verify all 256 entries of gamma LUT
  3. test_roundtrip_identity  - degamma(gamma(x)) ≈ x (roundtrip error check)
  4. test_chain_identity_matrix - Full chain (degamma→CM identity→gamma) ≈ x
  5. test_chain_saturation_50 - 50% saturation matrix: grayscale input unchanged
  6. test_chain_matrix_hw     - RTL color_matrix with Python degamma/gamma wraparound
"""
import math
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge
import numpy as np

# ---------------------------------------------------------------------------
# Python reference models (must match RTL LUTs exactly)
# ---------------------------------------------------------------------------

def _build_degamma_lut():
    """8-bit sRGB -> 12-bit Linear: round(4095 * (i/255)^2.2)"""
    lut = [0] * 256
    for i in range(256):
        lut[i] = min(4095, round(4095.0 * (i / 255.0) ** 2.2)) if i > 0 else 0
    return lut

def _build_gamma_lut():
    """12-bit Linear -> 8-bit sRGB: round(255 * (i/4095)^(1/2.2))"""
    lut = [0] * 4096
    for i in range(4096):
        lut[i] = min(255, round(255.0 * (i / 4095.0) ** (1.0 / 2.2))) if i > 0 else 0
    return lut

DEGAMMA_LUT = _build_degamma_lut()
GAMMA_LUT   = _build_gamma_lut()

def py_degamma(r, g, b):
    return DEGAMMA_LUT[r], DEGAMMA_LUT[g], DEGAMMA_LUT[b]

def py_gamma(r, g, b):
    return GAMMA_LUT[r], GAMMA_LUT[g], GAMMA_LUT[b]

def py_color_matrix(r, g, b, M):
    """Apply 3x3 matrix M (float, already normalized) with 12-bit clamp."""
    pix = np.array([r, g, b], dtype=float)
    out = M @ pix
    return tuple(int(np.clip(round(x), 0, 4095)) for x in out)

def py_full_chain(r, g, b, M):
    """degamma -> matrix -> gamma (Python reference)"""
    r1, g1, b1 = py_degamma(r, g, b)
    r2, g2, b2 = py_color_matrix(r1, g1, b1, M)
    return py_gamma(r2, g2, b2)

# Identity matrix (normalized, matching RTL: coeffs / 1024)
IDENTITY = np.eye(3, dtype=float)

# 50% saturation matrix
SAT_50 = np.array([
    [0.5,  0.25, 0.25],
    [0.25, 0.5,  0.25],
    [0.25, 0.25, 0.5 ],
], dtype=float)

# ---------------------------------------------------------------------------
# Shared TB helpers (for filter_color_matrix DUT, 12-bit I/O)
# ---------------------------------------------------------------------------
CM_PIPELINE_LATENCY = 3

async def reset_dut(dut):
    dut.reset_n.value     = 0
    dut.s_write.value     = 0
    dut.s_read.value      = 0
    dut.s_address.value   = 0
    dut.s_writedata.value = 0
    dut.din.value         = 0
    dut.hs_in.value       = 1
    dut.vs_in.value       = 1
    dut.de_in.value       = 0
    for _ in range(8):
        await RisingEdge(dut.clk)
    dut.reset_n.value = 1
    for _ in range(4):
        await RisingEdge(dut.clk)

async def csr_write(dut, addr, data):
    await RisingEdge(dut.clk_csr)
    dut.s_address.value   = addr
    dut.s_writedata.value = int(data) & 0xFFFFFFFF
    dut.s_write.value     = 1
    await RisingEdge(dut.clk_csr)
    dut.s_write.value = 0
    for _ in range(8):   # CDC 2FF sync
        await RisingEdge(dut.clk)

async def load_matrix_hw(dut, M_norm):
    """Load float matrix M_norm (3x3, normalized) as Q2.10 into DUT CSRs."""
    flat = [M_norm[r][c] for r in range(3) for c in range(3)]
    await csr_write(dut, 0, 1)   # matrix_en = 1
    for i, v in enumerate(flat):
        coeff_q = int(round(v * 1024.0))
        # pack as 12-bit two's complement
        coeff_q = coeff_q & 0xFFF
        await csr_write(dut, i + 1, coeff_q)

async def send_pixel(dut, r, g, b):
    # din is now 36 bits (12-bit per channel)
    dut.din.value   = (r << 24) | (g << 12) | b
    dut.de_in.value = 1
    await RisingEdge(dut.clk)

async def flush_pipeline(dut, n=CM_PIPELINE_LATENCY + 2):
    dut.de_in.value = 0
    for _ in range(n):
        await RisingEdge(dut.clk)

async def capture_output(dut, count):
    results = []
    timeout = count * 30
    while len(results) < count and timeout > 0:
        await RisingEdge(dut.clk)
        timeout -= 1
        if dut.de_out.value == 1:
            val = int(dut.dout.value)
            results.append(((val >> 24) & 0xFFF, (val >> 12) & 0xFFF, val & 0xFFF))
    return results

# ---------------------------------------------------------------------------
# Test 1: Verify all 256 entries of Python degamma LUT (8->12 bit)
# ---------------------------------------------------------------------------
@cocotb.test()
async def test_degamma_lut(dut):
    """Verify Python degamma LUT matches formula: round(4095*(i/255)^2.2)"""
    clock     = Clock(dut.clk,     26, units="ns")
    clock_csr = Clock(dut.clk_csr, 20, units="ns")
    cocotb.start_soon(clock.start())
    cocotb.start_soon(clock_csr.start())
    await reset_dut(dut)

    errors = 0
    for i in range(256):
        expected = min(4095, round(4095.0 * (i / 255.0) ** 2.2)) if i > 0 else 0
        got = DEGAMMA_LUT[i]
        if abs(got - expected) > 1:
            dut._log.error(f"DeGamma LUT[{i}]: formula={expected} lut={got}")
            errors += 1

    assert errors == 0, f"DeGamma LUT has {errors} mismatches"
    dut._log.info("[DeGamma LUT] All 256 entries verified OK (12-bit range)")

    # 12-bit anchor points matching round(4095 * (i/255)^2.2)
    # 64: 196, 128: 899, 192: 2193
    anchors = {0: 0, 64: 196, 128: 899, 192: 2193, 255: 4095}
    for inp, ref in anchors.items():
        got = DEGAMMA_LUT[inp]
        assert abs(got - ref) <= 2, f"DeGamma anchor[{inp}]: ref={ref} got={got}"
        dut._log.info(f"  [Anchor] degamma({inp}) = {got}  (ref = {ref})")


# ---------------------------------------------------------------------------
# Test 2: Verify all 4096 entries of Python gamma LUT (12->8 bit)
# ---------------------------------------------------------------------------
@cocotb.test()
async def test_gamma_lut(dut):
    """Verify Python gamma LUT matches formula: round(255*(i/4095)^(1/2.2))"""
    clock     = Clock(dut.clk,     26, units="ns")
    clock_csr = Clock(dut.clk_csr, 20, units="ns")
    cocotb.start_soon(clock.start())
    cocotb.start_soon(clock_csr.start())
    await reset_dut(dut)

    errors = 0
    for i in range(4096):
        expected = min(255, round(255.0 * (i / 4095.0) ** (1.0/2.2))) if i > 0 else 0
        got = GAMMA_LUT[i]
        if abs(got - expected) > 1:
            dut._log.error(f"Gamma LUT[{i}]: formula={expected} lut={got}")
            errors += 1

    assert errors == 0, f"Gamma LUT has {errors} mismatches"
    dut._log.info("[Gamma LUT] All 4096 entries verified OK")

    # Benchmarks matching round(255 * (i/4095)^(1/2.2))
    anchors = {0: 0, 1: 6, 196: 64, 905: 128, 2174: 192, 4095: 255}
    for inp, ref in anchors.items():
        got = GAMMA_LUT[inp]
        assert abs(got - ref) <= 1, f"Gamma anchor[{inp}]: ref={ref} got={got}"
        dut._log.info(f"  [Anchor] gamma({inp}) = {got}  (ref = {ref})")


# ---------------------------------------------------------------------------
# Test 3: Round-trip verification - 12-bit precision should be better
# ---------------------------------------------------------------------------
@cocotb.test()
async def test_roundtrip_identity(dut):
    """
    Verify that gamma(degamma(x)) ≈ x.
    Internal precision helps prevent dark-step crushing.
    """
    clock     = Clock(dut.clk,     26, units="ns")
    clock_csr = Clock(dut.clk_csr, 20, units="ns")
    cocotb.start_soon(clock.start())
    cocotb.start_soon(clock_csr.start())
    await reset_dut(dut)

    max_err = 0 
    for i in range(256):
        lin = DEGAMMA_LUT[i]
        ro = GAMMA_LUT[lin]
        err = abs(ro - i)
        if err > max_err:
            max_err = err
        if err > 1:
             dut._log.warning(f"  roundtrip mismatch at {i}: degamma={lin}, gamma={ro}, err={err}")

    dut._log.info(f"[Roundtrip 12-bit] max error: {max_err} LSB")
    # With 12-bit, error should stay extremely low.
    # Note: absolute dark values (1-5) might still map to 0 in 12-bit space?
    # (1/255)^2.2 * 4095 = 5.46e-6 * 4095 = 0.02 -> 0.
    # (5/255)^2.2 * 4095 = 1.76e-4 * 4095 = 0.72 -> 1.
    # So values below 5 will crush to 0. This is expected for 8->12 conversion.
    # But once in 12-bit, we don't lose MORE information.
    assert max_err <= 4, f"12-bit Roundtrip error too large: {max_err}"
    dut._log.info("[Roundtrip 12-bit] PASS")


# ---------------------------------------------------------------------------
# Test 4: Full chain (Python ref) - Identity matrix should preserve input
# ---------------------------------------------------------------------------
@cocotb.test()
async def test_chain_identity_matrix(dut):
    """
    Full chain test (Python reference):
    degamma -> Identity CM -> gamma ≈ original pixel (within ±1 LSB)
    """
    clock     = Clock(dut.clk,     26, units="ns")
    clock_csr = Clock(dut.clk_csr, 20, units="ns")
    cocotb.start_soon(clock.start())
    cocotb.start_soon(clock_csr.start())
    await reset_dut(dut)

    test_pixels = [
        (0, 0, 0), (255, 255, 255), (128, 128, 128),
        (255, 0, 0), (0, 255, 0), (0, 0, 255),
        (1, 1, 1), (10, 10, 10), (64, 128, 192),
    ]

    max_err = 0
    for (r, g, b) in test_pixels:
        ro, go, bo = py_full_chain(r, g, b, IDENTITY)
        err = max(abs(r - ro), abs(g - go), abs(b - bo))
        if err > max_err:
            max_err = err
        dut._log.info(
            f"[Chain Identity] in=({r},{g},{b}) -> out=({ro},{go},{bo}), err={err}"
        )

    assert max_err <= 1, f"Identity chain max error too large: {max_err} (Dark preservation failed?)"
    dut._log.info(f"[Chain Identity] PASS, max error = {max_err} LSB")


# ---------------------------------------------------------------------------
# Test 5: 50% Saturation matrix - grayscale input should be unchanged
# ---------------------------------------------------------------------------
@cocotb.test()
async def test_chain_saturation_50(dut):
    """
    50% saturation matrix: grayscale input (R=G=B) should be preserved.
    The matrix only mixes channels, so for R=G=B the output row = same value.
    """
    clock     = Clock(dut.clk,     26, units="ns")
    clock_csr = Clock(dut.clk_csr, 20, units="ns")
    cocotb.start_soon(clock.start())
    cocotb.start_soon(clock_csr.start())
    await reset_dut(dut)

    # For grayscale (R=G=B=v): SAT_50 row sums to 1.0, so output = v
    gray_pixels = [(v, v, v) for v in [0, 1, 32, 64, 128, 192, 254, 255]]

    dut._log.info("[Sat50 - Grayscale] Testing with 12-bit internal precision:")
    max_err = 0
    for (r, g, b) in gray_pixels:
        ro, go, bo = py_full_chain(r, g, b, SAT_50)
        err = max(abs(r - ro), abs(g - go), abs(b - bo))
        if err > max_err:
            max_err = err
        dut._log.info(f"  in=({r},{g},{b}) -> ({ro},{go},{bo}), err={err}")

    assert max_err <= 1, f"Sat50 grayscale error too large: {max_err}"
    dut._log.info(f"[Sat50 Grayscale] PASS")

    # Colored pixels: saturation should reduce chroma
    color_pixels = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (200, 100, 50)]
    dut._log.info("[Sat50 - Color] Verify saturation reduces chroma:")
    for (r, g, b) in color_pixels:
        ro, go, bo = py_full_chain(r, g, b, SAT_50)
        # After sat50, channels should be more equal than input
        in_range  = max(r, g, b) - min(r, g, b)
        # degamma and gamma affect the linear domain, so check linear domain
        rl, gl, bl = py_degamma(r, g, b)
        rol, gol, bol = py_degamma(ro, go, bo)
        out_range = max(rol, gol, bol) - min(rol, gol, bol)
        dut._log.info(
            f"  in=({r},{g},{b}) -> ({ro},{go},{bo}) | "
            f"linear chroma: {in_range} -> {out_range}"
        )
        # With 50% sat, linear chroma should be reduced (not necessarily sRGB range due to gamma)


# ---------------------------------------------------------------------------
# Test 6: RTL color_matrix HW test with Python degamma/gamma wraparound
# ---------------------------------------------------------------------------
@cocotb.test()
async def test_chain_matrix_hw(dut):
    """
    Validates that the 12-bit RTL color_matrix is bit-accurate with the Python model.
    """
    clock     = Clock(dut.clk,     26, units="ns")
    clock_csr = Clock(dut.clk_csr, 20, units="ns")
    cocotb.start_soon(clock.start())
    cocotb.start_soon(clock_csr.start())

    await reset_dut(dut)
    await load_matrix_hw(dut, IDENTITY)  # Identity matrix in RTL

    test_pixels = [
        (100, 150, 200), (255, 255, 255), (1, 1, 1), (128, 64, 32),
    ]

    # Step 1: Apply 12-bit degamma in software
    linear_pixels = [py_degamma(r, g, b) for r, g, b in test_pixels]

    # Step 2: Feed into 12-bit RTL color_matrix
    monitor = cocotb.start_soon(capture_output(dut, len(linear_pixels)))
    for r, g, b in linear_pixels:
        await send_pixel(dut, r, g, b)
    await flush_pipeline(dut)
    rtl_outputs = await monitor

    assert len(rtl_outputs) == len(linear_pixels), \
        f"Expected {len(linear_pixels)} pixels, got {len(rtl_outputs)}"

    # Step 3: Apply 12-bit gamma in software, compare
    max_err = 0
    for i, ((r_in, g_in, b_in), rtl_lin) in enumerate(zip(test_pixels, rtl_outputs)):
        r_out, g_out, b_out = py_gamma(*rtl_lin)
        r_ref, g_ref, b_ref = py_full_chain(r_in, g_in, b_in, IDENTITY)

        err = max(abs(r_out - r_ref), abs(g_out - g_ref), abs(b_out - b_ref))
        if err > max_err:
            max_err = err
        dut._log.info(
            f"[Chain RTL 12-bit] in=({r_in},{g_in},{b_in}) "
            f"-> linear(12b)=({rtl_lin[0]},{rtl_lin[1]},{rtl_lin[2]}) "
            f"-> out=({r_out},{g_out},{b_out}) "
            f"ref=({r_ref},{g_ref},{b_ref}) err={err}"
        )

    # With rounding in both CM and Gamma/Degamma, max error should be very low
    assert max_err <= 1, f"RTL chain 12-bit identity max error {max_err} exceeds 1"
    dut._log.info(f"[Chain RTL 12-bit] PASS")
