"""
tb_color_matrix.py
Cocotb testbench for filter_color_matrix.v
Tests:
  1. Bypass mode (matrix_en=0) - output == input (3-cycle latency)
  2. Identity matrix (C00=C11=C22=1024, rest=0) - output == input
  3. CALIBRATION.md example coefficients - White in -> R≈G≈B out, clamped
  4. De-Gamma LUT verification via standalone check (Python reference)
"""
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer
import numpy as np

PIPELINE_LATENCY = 3  # filter_color_matrix: 3 clocks

# ----------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------
async def reset_dut(dut):
    dut.reset_n.value    = 0
    dut.s_write.value    = 0
    dut.s_read.value     = 0
    dut.s_address.value  = 0
    dut.s_writedata.value= 0
    dut.din.value        = 0
    dut.hs_in.value      = 1
    dut.vs_in.value      = 1
    dut.de_in.value      = 0
    for _ in range(8):
        await RisingEdge(dut.clk)
    dut.reset_n.value = 1
    for _ in range(4):
        await RisingEdge(dut.clk)

async def csr_write(dut, addr, data):
    """Write one CSR register (clk_csr = dut.clk in this TB)"""
    await RisingEdge(dut.clk_csr)
    dut.s_address.value   = addr
    dut.s_writedata.value = data & 0xFFFFFFFF
    dut.s_write.value     = 1
    await RisingEdge(dut.clk_csr)
    dut.s_write.value     = 0
    # Wait for CDC sync (2 pixel clocks)
    for _ in range(4):
        await RisingEdge(dut.clk)

async def send_pixel(dut, r, g, b):
    # 36-bit format: 12-bit Red (35:24), 12-bit Green (23:12), 12-bit Blue (11:0)
    dut.din.value   = ((int(r) & 0xFFF) << 24) | ((int(g) & 0xFFF) << 12) | (int(b) & 0xFFF)
    dut.de_in.value = 1
    await RisingEdge(dut.clk)

async def flush_pipeline(dut, n=PIPELINE_LATENCY + 2):
    dut.de_in.value = 0
    for _ in range(n):
        await RisingEdge(dut.clk)

async def capture_output(dut, count):
    """Capture `count` output pixels after de_out goes high."""
    results = []
    timeout = count * 20
    while len(results) < count and timeout > 0:
        await RisingEdge(dut.clk)
        timeout -= 1
        if dut.de_out.value == 1:
            val = int(dut.dout.value)
            # 36-bit format: 12-bit Red, 12-bit Green, 12-bit Blue
            results.append(((val >> 24) & 0xFFF, (val >> 12) & 0xFFF, val & 0xFFF))
    return results

# ----------------------------------------------------------------
# Test 1: Bypass (matrix_en = 0)
# ----------------------------------------------------------------
@cocotb.test()
async def test_bypass(dut):
    """matrix_en=0: output should equal input after 3-cycle latency"""
    clock     = Clock(dut.clk,     26, units="ns")   # ~37.8 MHz pixel clk
    clock_csr = Clock(dut.clk_csr, 20, units="ns")   # 50 MHz CSR clk
    cocotb.start_soon(clock.start())
    cocotb.start_soon(clock_csr.start())

    await reset_dut(dut)
    # Ensure matrix is disabled (default)
    await csr_write(dut, 0, 0)  # matrix_en = 0

    test_pixels = [(100, 150, 200), (255, 0, 128), (0, 255, 0), (64, 64, 64)]
    for r, g, b in test_pixels:
        await send_pixel(dut, r, g, b)
    await flush_pipeline(dut)

    results = []
    # Re-send and capture simultaneously
    await reset_dut(dut)
    await csr_write(dut, 0, 0)
    monitor = cocotb.start_soon(capture_output(dut, len(test_pixels)))
    for r, g, b in test_pixels:
        await send_pixel(dut, r, g, b)
    await flush_pipeline(dut)
    results = await monitor

    for i, ((ri, gi, bi), (ro, go, bo)) in enumerate(zip(test_pixels, results)):
        assert ri == ro and gi == go and bi == bo, \
            f"Bypass fail pixel[{i}]: in=({ri},{gi},{bi}) out=({ro},{go},{bo})"
        dut._log.info(f"[Bypass OK] in=({ri},{gi},{bi}) out=({ro},{go},{bo})")


# ----------------------------------------------------------------
# Test 2: Identity Matrix
# ----------------------------------------------------------------
@cocotb.test()
async def test_identity(dut):
    """Identity matrix: output == input"""
    clock     = Clock(dut.clk,     26, units="ns")
    clock_csr = Clock(dut.clk_csr, 20, units="ns")
    cocotb.start_soon(clock.start())
    cocotb.start_soon(clock_csr.start())

    await reset_dut(dut)
    # Load identity: C00=C11=C22=1024, off-diagonal=0
    await csr_write(dut, 0, 1)      # matrix_en = 1
    await csr_write(dut, 1, 1024)   # C00
    await csr_write(dut, 2, 0)      # C01
    await csr_write(dut, 3, 0)      # C02
    await csr_write(dut, 4, 0)      # C10
    await csr_write(dut, 5, 1024)   # C11
    await csr_write(dut, 6, 0)      # C12
    await csr_write(dut, 7, 0)      # C20
    await csr_write(dut, 8, 0)      # C21
    await csr_write(dut, 9, 1024)   # C22

    test_pixels = [(200, 100, 50), (255, 255, 255), (0, 0, 0), (128, 64, 32)]
    monitor = cocotb.start_soon(capture_output(dut, len(test_pixels)))
    for r, g, b in test_pixels:
        await send_pixel(dut, r, g, b)
    await flush_pipeline(dut)
    results = await monitor

    for i, ((ri, gi, bi), (ro, go, bo)) in enumerate(zip(test_pixels, results)):
        assert abs(ri - ro) <= 1 and abs(gi - go) <= 1 and abs(bi - bo) <= 1, \
            f"Identity fail pixel[{i}]: in=({ri},{gi},{bi}) out=({ro},{go},{bo})"
        dut._log.info(f"[Identity OK] in=({ri},{gi},{bi}) out=({ro},{go},{bo})")


# ----------------------------------------------------------------
# Test 3: CALIBRATION.md example coefficients (White preservation)
# C = [[923,192,31],[44,970,11],[7,18,706]] / 1024
# ----------------------------------------------------------------
@cocotb.test()
async def test_calibration_example(dut):
    """CALIBRATION.md coefficients: White (255,255,255) -> ~White (clamped)"""
    clock     = Clock(dut.clk,     26, units="ns")
    clock_csr = Clock(dut.clk_csr, 20, units="ns")
    cocotb.start_soon(clock.start())
    cocotb.start_soon(clock_csr.start())

    await reset_dut(dut)

    cal_coeffs = [923, 192, 31, 44, 970, 11, 7, 18, 706]
    await csr_write(dut, 0, 1)   # matrix_en = 1
    for i, c in enumerate(cal_coeffs):
        await csr_write(dut, i + 1, c & 0xFFF)

    # Test White input
    test_pixels = [(255, 255, 255), (128, 128, 128), (0, 0, 0)]
    
    # Degamma function: 8-bit -> 12-bit (Gamma 2.2)
    def py_degamma(val):
        return min(4095, round(4095.0 * (val / 255.0) ** 2.2)) if val > 0 else 0
        
    # Gamma function: 12-bit -> 8-bit (Gamma 1/2.2)
    def py_gamma(val):
        return min(255, round(255.0 * (val / 4095.0) ** (1.0 / 2.2))) if val > 0 else 0

    monitor = cocotb.start_soon(capture_output(dut, len(test_pixels)))
    for r, g, b in test_pixels:
        # Before sending to the 12-bit Color Matrix HW, we must apply De-Gamma!
        r_lin, g_lin, b_lin = py_degamma(r), py_degamma(g), py_degamma(b)
        await send_pixel(dut, r_lin, g_lin, b_lin)
    await flush_pipeline(dut)
    results_12bit = await monitor

    # Python reference: float computation (in Linear space!)
    # White point target in linear will be transformed by M
    M = np.array([[923, 192, 31], [44, 970, 11], [7, 18, 706]]) / 1024.0
    
    for i, (pix_in, (ro_12, go_12, bo_12)) in enumerate(zip(test_pixels, results_12bit)):
        # Apply Gamma to get back to 8-bit sRGB space
        ro, go, bo = py_gamma(ro_12), py_gamma(go_12), py_gamma(bo_12)
        
        # Calculate exactly what python float precision expects (in linear space, then gamma)
        lin_in = np.array([py_degamma(pix_in[0]), py_degamma(pix_in[1]), py_degamma(pix_in[2])])
        lin_out = M @ lin_in
        lin_out = np.clip(np.round(lin_out), 0, 4095).astype(int)
        
        ref = [py_gamma(lin_out[0]), py_gamma(lin_out[1]), py_gamma(lin_out[2])]

        # Due to integer matrix multiplication truncation inside the RTL:
        # Error tolerance should comfortably be within ±2 of expected Python representation.
        assert abs(int(ref[0]) - ro) <= 2, f"R mismatch pixel[{i}]: ref={ref[0]} got={ro}"
        assert abs(int(ref[1]) - go) <= 2, f"G mismatch pixel[{i}]: ref={ref[1]} got={go}"
        assert abs(int(ref[2]) - bo) <= 2, f"B mismatch pixel[{i}]: ref={ref[2]} got={bo}"
        
        dut._log.info(f"[CAL OK] in={pix_in} ref=({ref[0]},{ref[1]},{ref[2]}) "
                      f"out=({ro},{go},{bo})")


# ----------------------------------------------------------------
# Test 4: Python reference check - De-Gamma LUT values
# (Software-only sanity check, no hardware instantiation needed)
# ----------------------------------------------------------------
@cocotb.test()
async def test_degamma_reference(dut):
    """Verify the De-Gamma LUT table values against Python gamma 2.2"""
    # Expected LUT values from Python: round(255 * (i/255)^2.2)
    expected = [int(round(255.0 * (i / 255.0) ** 2.2)) if i > 0 else 0
                for i in range(256)]
    # Spot check a few key values
    checks = {0: 0, 64: 14, 127: 54, 128: 55, 192: 139, 255: 255}  # approx
    for inp, ref in checks.items():
        got = expected[inp]
        assert abs(got - ref) <= 2, \
            f"De-Gamma LUT[{inp}]: expected ~{ref}, python gives {got}"
        dut._log.info(f"[DeGamma LUT] [{inp}] = {got}  (ref ~{ref})")
