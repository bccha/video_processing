import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, ReadOnly
import numpy as np
from PIL import Image
import os

# Video settings
WIDTH = 960
HEIGHT = 540
H_BLANK = 160
H_TOTAL = WIDTH + H_BLANK

async def reset_dut(dut):
    dut.reset_n.value = 0
    dut.din.value = 0
    dut.hs_in.value = 0
    dut.vs_in.value = 0
    dut.de_in.value = 0
    # Set default filter mode (e.g., 0: Bypass)
    dut.filter_mode.value = 0
    for _ in range(10):
        await RisingEdge(dut.clk)
    dut.reset_n.value = 1
    for _ in range(5):
        await RisingEdge(dut.clk)

# Output monitor coroutine
async def monitor_output(dut, out_pixels):
    y = 0
    x = 0
    while True:
        await RisingEdge(dut.clk)
        await ReadOnly()
        if dut.de_out.value == 1:
            val = int(dut.dout.value)
            # RTL assumes RGB in [23:0]. 
            # If we feed BGR from file, we need to know how it's mapped.
            # Assuming din[23:16]=R, [15:8]=G, [7:0]=B
            r = (val >> 16) & 0xFF
            g = (val >> 8) & 0xFF
            b = val & 0xFF
            
            if y < HEIGHT and x < WIDTH:
                out_pixels[y, x] = [r, g, b]
            
            x += 1
            if x >= WIDTH:
                x = 0
                y += 1
        
        if y >= HEIGHT:
            break

@cocotb.test()
async def tb_image_filter(dut):
    """Test image_filter RTL using image.raw (BGR format)"""
    clock = Clock(dut.clk, 26, units="ns") 
    cocotb.start_soon(clock.start())
    
    # Get current test directory
    cocotb_dir = os.path.dirname(os.path.abspath(__file__))
    proj_dir = os.path.dirname(os.path.dirname(cocotb_dir))
    raw_path = os.path.join(proj_dir, "linux_software", "image_converter", "image.raw")

    # Set mode from environment or default to 0
    mode = int(os.environ.get('FILTER_MODE', 0))
    dut._log.info(f"Setting FILTER_MODE to {mode}")
    
    await reset_dut(dut)
    dut.filter_mode.value = mode
    
    try:
        # User says it's BGR. raw file is BGRA 32-bit.
        raw_data = np.fromfile(raw_path, dtype=np.uint8).reshape((HEIGHT, WIDTH, 4))
        bgr = raw_data[:, :, :3]
        # Convert BGR to RGB for the RTL input (which expects din[23:16]=R)
        rgb_input = np.stack([bgr[:,:,2], bgr[:,:,1], bgr[:,:,0]], axis=-1)
    except FileNotFoundError:
        dut._log.error(f"Could not find {raw_path}")
        return

    out_pixels = np.zeros((HEIGHT, WIDTH, 3), dtype=np.uint8)
    
    # Start monitor
    monitor_task = cocotb.start_soon(monitor_output(dut, out_pixels))
    
    dut._log.info("Starting frame stream...")
    
    # Start with VBlank
    for _ in range(2 * H_TOTAL):
        dut.de_in.value = 0
        dut.vs_in.value = 1
        await RisingEdge(dut.clk)
        
    # Active frame
    for y in range(HEIGHT):
        dut.vs_in.value = 0
        for x in range(WIDTH):
            r, g, b = rgb_input[y, x]
            dut.din.value = (int(r) << 16) | (int(g) << 8) | int(b)
            dut.de_in.value = 1
            await RisingEdge(dut.clk)
            
        # Horizontal Blanking
        for h in range(H_BLANK):
            dut.din.value = 0
            dut.de_in.value = 0
            await RisingEdge(dut.clk)
            
    # Post-VBlank to flush
    for _ in range(5 * H_TOTAL):
        dut.de_in.value = 0
        dut.vs_in.value = 1
        await RisingEdge(dut.clk)
        
    await monitor_task
    
    dut._log.info("Finished streaming input. Saving image...")
    # Save with mode suffix
    out_name = f"rtl_out_mode_{mode}.jpg"
    Image.fromarray(out_pixels, mode='RGB').save(out_name)
    dut._log.info(f"Saved {out_name}")
