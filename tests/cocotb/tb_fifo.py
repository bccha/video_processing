import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, ReadOnly, Timer
from cocotb.queue import Queue
import random

class FIFO_Driver:
    def __init__(self, dut, name, clock):
        self.dut = dut
        self.name = name
        self.clock = clock
        self.queue = Queue()

    async def write(self, data):
        while self.dut.wrfull.value:
             await RisingEdge(self.clock)
        self.dut.wrreq.value = 1
        self.dut.data.value = data
        await RisingEdge(self.clock)
        self.dut.wrreq.value = 0
        self.queue.put_nowait(data)

@cocotb.test()
async def test_fifo_basic(dut):
    """Basic Write and Read Test"""
    cocotb.start_soon(Clock(dut.wrclk, 10, units="ns").start()) # 100MHz
    cocotb.start_soon(Clock(dut.rdclk, 13.46, units="ns").start()) # ~74.25MHz

    dut.wrreq.value = 0
    dut.rdreq.value = 0
    
    await Timer(50, units="ns")
    
    # Write 10 values
    for i in range(10):
        dut.data.value = i
        dut.wrreq.value = 1
        await RisingEdge(dut.wrclk)
    dut.wrreq.value = 0
    
    await Timer(50, units="ns")
    
    # Read 10 values
    for i in range(10):
        # Wait until not empty
        while dut.rdempty.value:
            await RisingEdge(dut.rdclk)
        
        # Issue Read
        dut.rdreq.value = 1
        await RisingEdge(dut.rdclk)
        dut.rdreq.value = 0
        
        # Wait for data (latency 1 cycle, already passed by RisingEdge above? 
        # Logic: q updates at posedge rdclk. 
        # At Time T (RisingEdge), we set rdreq=1.
        # At Time T+1 (RisingEdge), q updates.
        # So we need to wait ONE MORE edge?
        # Code: always @(posedge rdclk) if (rdreq) q <= mem...
        # Yes, q updates at the NEXT edge.
        await RisingEdge(dut.rdclk) 
        
        await ReadOnly()
        read_val = int(dut.q.value)
        assert read_val == i, f"Expected {i}, got {read_val}"
        
        # Correctly exit ReadOnly phase before next loop might drive signals
        await RisingEdge(dut.rdclk) 

@cocotb.test()
async def test_fifo_flags(dut):
    """Test Full and Empty Flags"""
    cocotb.start_soon(Clock(dut.wrclk, 10, units="ns").start())
    cocotb.start_soon(Clock(dut.rdclk, 23, units="ns").start()) # Different freq
    
    dut.wrreq.value = 0
    dut.rdreq.value = 0
    
    # Wait for initial sync
    await Timer(100, units="ns")
    
    # Debug: Print pointers
    dut._log.info(f"rdempty: {dut.rdempty.value}")
    try:
        dut._log.info(f"rd_ptr_gray: {dut.rd_ptr_gray.value}")
        dut._log.info(f"wr_ptr_gray_sync2: {dut.wr_ptr_gray_sync2.value}")
    except:
        dut._log.info("Could not access internal signals directly")

    assert dut.rdempty.value == 1, "Should be empty initially"
    assert dut.wrfull.value == 0, "Should not be full initially"
    
    # Write 1
    dut.data.value = 0xAA
    dut.wrreq.value = 1
    await RisingEdge(dut.wrclk)
    dut.wrreq.value = 0
    
    # Wait for CDC (Writer -> Reader)
    # wr_ptr changes -> sync1 -> sync2 -> rdempty clears
    # At least 2-3 rdclk cycles
    for _ in range(5):
        await RisingEdge(dut.rdclk)
    
    assert dut.rdempty.value == 0, "Should not be empty after write"
    
    # Read 1
    dut.rdreq.value = 1
    await RisingEdge(dut.rdclk)
    dut.rdreq.value = 0
    
    # Check Empty again
    # rd_ptr changes -> sync1 -> sync2 -> wrfull clears (not checked here)
    # But rdempty is purely local to rdclk domain?
    # assign rdempty = (rd_ptr_gray == wr_ptr_gray_sync2);
    # rd_ptr_gray updates at rdclk. wr_ptr_gray_sync2 is stable (if no more writes).
    # So rdempty should assert immediately at next rdclk edge?
    await RisingEdge(dut.rdclk)
    await ReadOnly()
    assert dut.rdempty.value == 1, "Should be empty after reading all data"

@cocotb.test()
async def test_fifo_overflow_protection(dut):
    """Check wrusedw saturation mechanisms"""
    cocotb.start_soon(Clock(dut.wrclk, 10, units="ns").start())
    cocotb.start_soon(Clock(dut.rdclk, 10, units="ns").start())
    
    dut.wrreq.value = 0
    dut.rdreq.value = 0
    await Timer(100, units="ns")
    
    # Fill almost full
    depth = 512
    for i in range(depth - 1): 
        dut.data.value = i
        dut.wrreq.value = 1
        await RisingEdge(dut.wrclk)
    dut.wrreq.value = 0
    
    await RisingEdge(dut.wrclk)
    
    # Write one more to fill
    dut.data.value = 0xFF
    dut.wrreq.value = 1
    await RisingEdge(dut.wrclk)
    dut.wrreq.value = 0
    
    await RisingEdge(dut.wrclk)
    await ReadOnly()
    
    assert dut.wrfull.value == 1, "Should be full"
    
    used = int(dut.wrusedw.value)
    # 511 is the saturated max for 9-bit width (which is default ADDR_WIDTH=9)
    assert used == 511, f"Used words should be saturated to 511, got {used}"
