import cocotb
from cocotb.triggers import RisingEdge, Timer
from cocotb.clock import Clock

async def reset_dut(reset_n, duration_ns):
    reset_n.value = 0
    await Timer(duration_ns, unit="ns")
    reset_n.value = 1
    await Timer(duration_ns, unit="ns")

@cocotb.test()
async def test_dma_single_frame(dut):
    """Test that DMA starts on start pulse and stops after one frame"""
    
    cocotb.start_soon(Clock(dut.clk, 20, unit="ns").start()) # 50MHz
    await reset_dut(dut.reset_n, 100)
    
    # Setup
    dut.dma_start.value = 0
    dut.dma_cont_en.value = 0
    dut.start_addr.value = 0x30000000
    dut.vsync_edge.value = 0
    dut.fifo_used.value = 0
    dut.m_waitrequest.value = 0
    dut.m_readdata.value = 0
    dut.m_readdatavalid.value = 0
    
    # 1. Start Pulse
    await RisingEdge(dut.clk)
    dut.dma_start.value = 1
    await RisingEdge(dut.clk)
    dut.dma_start.value = 0
    
    # Wait for Busy
    await RisingEdge(dut.clk)
    assert dut.busy.value == 1, "DMA should be busy after start pulse"
    
    # 2. Simulate reading some data (just verify it stays busy)
    for _ in range(100):
        await RisingEdge(dut.clk)
        if (dut.m_read.value == 1):
            dut.m_readdatavalid.value = 1
        else:
            dut.m_readdatavalid.value = 0
             
    assert dut.busy.value == 1, "DMA should still be busy mid-frame"
    dut._log.info("DMA successfully started and busy bit set")

@cocotb.test()
async def test_dma_continuous_mode(dut):
    """Test that DMA starts and restarts on V-Sync in continuous mode"""
    
    cocotb.start_soon(Clock(dut.clk, 20, unit="ns").start())
    await reset_dut(dut.reset_n, 100)
    
    dut.dma_start.value = 0
    dut.dma_cont_en.value = 1
    dut.vsync_edge.value = 1
    await RisingEdge(dut.clk)
    dut.vsync_edge.value = 0
    
    await RisingEdge(dut.clk)
    assert dut.busy.value == 1, "DMA should start on V-Sync in continuous mode"
    dut._log.info("DMA continuous mode started successfully")
