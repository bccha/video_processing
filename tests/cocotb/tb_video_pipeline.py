import cocotb
from cocotb.triggers import RisingEdge, Timer
from cocotb.clock import Clock

async def reset_pipeline(dut, duration_ns):
    dut.reset_n.value = 0
    await Timer(duration_ns, unit="ns")
    dut.reset_n.value = 1
    await Timer(duration_ns, unit="ns")

@cocotb.test()
async def test_pipeline_dma_control(dut):
    """Test DMA Start/Busy/Done/Stop through registers"""
    
    cocotb.start_soon(Clock(dut.clk_50, 20, unit="ns").start())
    cocotb.start_soon(Clock(dut.clk_hdmi, 13468, unit="ps").start())
    
    await reset_pipeline(dut, 100)
    
    # 1. Switch to DMA Mode
    dut.s_address.value = 0
    dut.s_writedata.value = 8
    dut.s_write.value = 1
    await RisingEdge(dut.clk_hdmi)
    dut.s_write.value = 0

    # 2. Check initial Busy=0
    dut.s_address.value = 1
    dut.s_read.value = 1
    await RisingEdge(dut.clk_hdmi)
    await RisingEdge(dut.clk_hdmi) # Wait for valid
    assert not (int(dut.s_readdata.value) & (1 << 31)), "DMA should not be busy initially"
    dut.s_read.value = 0
    
    # 3. Issue Start Pulse (Addr 1, Bit 2)
    dut.s_address.value = 1
    dut.s_writedata.value = 0x04
    dut.s_write.value = 1
    await RisingEdge(dut.clk_hdmi)
    dut.s_write.value = 0
    
    # 4. Check for Busy bit (Wait for CDC)
    for _ in range(20):
        dut.s_address.value = 1
        dut.s_read.value = 1
        await RisingEdge(dut.clk_hdmi)
        await RisingEdge(dut.clk_hdmi)
        if (int(dut.s_readdata.value) & (1 << 31)):
            break
    assert (int(dut.s_readdata.value) & (1 << 31)), "Busy bit should be set after Start command"
    dut._log.info("DMA Busy bit detected correctly")
    
    # 5. Continuous Mode (Addr 1, Bit 1)
    dut.s_address.value = 1
    dut.s_writedata.value = 0x02
    dut.s_write.value = 1
    await RisingEdge(dut.clk_hdmi)
    dut.s_write.value = 0
    
    dut._log.info("DMA Control register test completed")
