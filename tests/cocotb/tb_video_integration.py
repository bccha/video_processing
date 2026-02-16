import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer, ReadOnly, ClockCycles
import os
import struct
import random
from cocotb.queue import Queue

class AvalonMemory:
    def __init__(self, dut, size=1024*1024):
        self.dut = dut
        self.mem = {}
        # Populate memory with pattern: 0x00RRGGBB
        for i in range(0, size, 4):
            val = (i // 4) & 0x00FFFFFF 
            self.mem[i] = val 
        
        self.req_queue = Queue()
            
    async def run(self):
        """Standard Avalon-MM Slave Responder"""
        self.dut.m_waitrequest.value = 0
        self.dut.m_readdatavalid.value = 0
        self.dut.m_readdata.value = 0
        
        cocotb.start_soon(self.response_driver())
        
        while True:
            await RisingEdge(self.dut.clk_50) 
            try:
                read_req = int(self.dut.m_read.value)
                wait_req = int(self.dut.m_waitrequest.value)
            except ValueError:
                read_req = 0
                wait_req = 0

            if read_req and not wait_req:
                try:
                    addr = int(self.dut.m_address.value)
                    burst = int(self.dut.m_burstcount.value)
                except ValueError:
                    continue
                self.req_queue.put_nowait((addr, burst))
                
    async def response_driver(self):
        while True:
            addr, burst = await self.req_queue.get()
            latency = random.randint(2, 10)
            for _ in range(latency):
                await RisingEdge(self.dut.clk_50)
                self.dut.m_readdatavalid.value = 0
            
            for i in range(burst):
                await RisingEdge(self.dut.clk_50)
                self.dut.m_readdatavalid.value = 1
                addr_cal = addr + (i * 4)
                data = self.mem.get(addr_cal, 0x000000)
                self.dut.m_readdata.value = data
            
            await RisingEdge(self.dut.clk_50)
            self.dut.m_readdatavalid.value = 0

async def configure_pipeline(dut):
    """Sets mode to DMA Stream through Nios II Slave interface"""
    # Mode 8: DMA Stream
    dut.s_address.value = 0
    dut.s_writedata.value = 8
    dut.s_write.value = 1
    await RisingEdge(dut.clk_50)
    dut.s_write.value = 0
    
    # Global Ctrl: Bit[1]=Continuous Enable
    dut.s_address.value = 1
    dut.s_writedata.value = 0x00000002 
    dut.s_write.value = 1
    await RisingEdge(dut.clk_50)
    dut.s_write.value = 0
    
    # Frame Pointer
    dut.s_address.value = 6
    dut.s_writedata.value = 0x00000000
    dut.s_write.value = 1
    await RisingEdge(dut.clk_50)
    dut.s_write.value = 0

@cocotb.test()
async def test_full_integration(dut):
    """
    Verify DMA reads -> FIFO -> HDMI Output (960x540 qHD)
    """
    cocotb.start_soon(Clock(dut.clk_50, 20, units="ns").start()) # 50 MHz
    cocotb.start_soon(Clock(dut.clk_hdmi, 26.43, units="ns").start()) # ~37.83 MHz
    
    debug_log_file = os.path.join(os.path.dirname(__file__), "debug_timing.log")
    if os.path.exists(debug_log_file): os.remove(debug_log_file)
    
    with open(debug_log_file, "a") as f: f.write("Testbench Start\n")
    
    # Initialize signals
    dut.reset_n.value = 0
    dut.s_read.value = 0
    dut.s_write.value = 0
    dut.m_waitrequest.value = 0
    dut.m_readdata.value = 0
    dut.m_readdatavalid.value = 0
    
    # Setup Memory Model
    mem_model = AvalonMemory(dut)
    image_path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(__file__))), 
                            "linux_software/image_converter/image.raw")
    if os.path.exists(image_path):
        try:
            with open(image_path, "rb") as f_img:
                raw_data = f_img.read()
                words = struct.unpack('<' + str(len(raw_data)//4) + 'I', raw_data)
                for i, word in enumerate(words):
                    mem_model.mem[i*4] = word
            with open(debug_log_file, "a") as f:
                 f.write(f"Loaded {len(words)} words into Memory Model\n")
        except Exception as e:
             dut._log.error(f"Failed to load image: {e}")

    cocotb.start_soon(mem_model.run())
    
    # Reset Sequence
    await Timer(100, units="ns")
    dut.reset_n.value = 1
    await ClockCycles(dut.clk_hdmi, 10)
    
    # 1. Configure Pipeline (Start DMA)
    await configure_pipeline(dut)
    
    # 2. Wait for FIFO to prime
    dut._log.info("Waiting for FIFO to prime...")
    for _ in range(1000):
        await RisingEdge(dut.clk_50)
        try:
            if int(dut.u_simple_fifo.wrusedw.value) > 32:
                break
        except: pass
        
    # 3. Fast-Forward to V-Sync start to avoid long wait
    # v_cnt=542 is just before V-Sync (starts at 543)
    dut._log.info("Fast-forwarding to V-Sync start...")
    try:
        dut.u_hdmi_sync.v_cnt.value = 542
        dut.u_hdmi_sync.h_cnt.value = 1110
    except: pass

    # 4. Wait for Frame Start (V-Sync Falling Edge)
    dut._log.info("Waiting for first Frame Start (VSync Falling)...")
    while True:
        await RisingEdge(dut.clk_hdmi)
        if int(dut.hdmi_vs.value) == 0: # VSync Active
            break
            
    while True:
        await RisingEdge(dut.clk_hdmi)
        if int(dut.hdmi_vs.value) == 1: # VSync Inactive (Frame Start)
            break
    
    dut._log.info("Frame Start detected! Starting 3-frame capture.")
    
    # Output file for HDMI data
    output_bin_file = os.path.join(os.path.dirname(__file__), "hdmi_output.bin")
    f_out = open(output_bin_file, "wb")
    
    pixel_count = 0
    frame_size = 960 * 540
    target_pixels = frame_size * 3
    
    dut._log.info(f"Starting Capture. Target: {target_pixels} pixels")
    
    vs_prev = 0
    de_prev = 0
    last_dma_addr = -1
    
    # Main simulation loop
    for cycle in range(5000000): # 5M cycles timeout
        await RisingEdge(dut.clk_hdmi)
        await ReadOnly() # CRITICAL: Exact sampling
        
        de = int(dut.hdmi_de.value)
        vs = int(dut.hdmi_vs.value)
        data = int(dut.hdmi_d.value)
        v_cnt = int(dut.u_hdmi_sync.v_cnt.value)
        h_cnt = int(dut.u_hdmi_sync.h_cnt.value)
        
        # Monitor DMA (Reduced for speed)
        # curr_dma_addr = int(dut.m_address.value)
        # if curr_dma_addr != last_dma_addr:
        #      last_dma_addr = curr_dma_addr
        
        # Log DE Transitions (Reduced for speed)
        if de != de_prev:
            de_prev = de
        if vs != vs_prev:
            vs_prev = vs

        # Capture Data
        if de == 1:
            f_out.write(struct.pack("<I", data))
            
            # Targeted debug around boundaries
            px_in_f = pixel_count % frame_size
            if px_in_f >= 950 and px_in_f <= 970:
                 with open(debug_log_file, "a") as f:
                     f.write(f"[DEBUG] Fx {pixel_count // frame_size} Px {px_in_f}: {data:06X} (FIFO={int(dut.u_simple_fifo.wrusedw.value)})\n")
                     
            pixel_count += 1
            if pixel_count >= target_pixels:
                break
                
        # Heartbeat
        if cycle % 500000 == 0:
            dut._log.info(f"Cycles: {cycle}, Captured: {pixel_count}")

    f_out.close()
    dut._log.info("Simulation Finished successfully.")
