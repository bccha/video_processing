import cocotb
from cocotb.triggers import RisingEdge, Timer
from cocotb.clock import Clock

async def reset_dut(reset_n, duration_ns):
    reset_n.value = 0
    await Timer(duration_ns, unit="ns")
    reset_n.value = 1
    await Timer(duration_ns, unit="ns")

@cocotb.test()
async def test_dma_control_registers(dut):
    """Test DMA Control Register (REG_GLOBAL_CTRL) logic"""
    
    # 1. Start Clock (74.25 MHz -> approx 13468 ps)
    cocotb.start_soon(Clock(dut.clk, 13468, unit="ps").start())
    
    # 2. Reset
    await reset_dut(dut.reset_n, 50)
    await RisingEdge(dut.clk)
    
    # 3. Check Default values (Should be 0)
    # Note: These ports will exist AFTER my RTL modification
    # Currently they might cause 'attribute not found' or similar if simulator supports it
    # But since I'm doing TDD, I expect failure.
    
    try:
        dma_en = dut.dma_enable_out.value
        dut._log.info(f"Initial DMA Enable: {dma_en}")
        assert dma_en == 0, "DMA Enable should be 0 by default"
    except AttributeError:
        dut._log.error("Port 'dma_enable_out' NOT FOUND in DUT!")
        raise AttributeError("Required port 'dma_enable_out' missing for DMA control test")

    # 4. Write to REG_GLOBAL_CTRL (Addr 1)
    # Bit 0: Gamma En, Bit 1: DMA En
    dut.avs_address.value = 1
    dut.avs_writedata.value = 0x02 # Enable DMA, Disable Gamma
    dut.avs_write.value = 1
    await RisingEdge(dut.clk)
    dut.avs_write.value = 0
    
    await RisingEdge(dut.clk)
    
    # 5. Check dma_enable_out
    assert dut.dma_enable_out.value == 1, "DMA Enable output should be 1 after writing 0x02 to Addr 1"
    
    # 6. Disable DMA
    dut.avs_address.value = 1
    dut.avs_writedata.value = 0x01 # Disable DMA, Enable Gamma
    dut.avs_write.value = 1
    await RisingEdge(dut.clk)
    dut.avs_write.value = 0
    
    await RisingEdge(dut.clk)
    assert dut.dma_enable_out.value == 0, "DMA Enable output should be 0 after writing 0x01 to Addr 1"
    dut._log.info("DMA Control Register Test PASSED")

@cocotb.test()
async def test_mode_out_connection(dut):
    """Test reg_mode_out port connection for video_pipeline synchronization"""
    
    cocotb.start_soon(Clock(dut.clk, 13468, unit="ps").start())
    await reset_dut(dut.reset_n, 50)
    
    try:
        # Write mode 8
        dut.avs_address.value = 0
        dut.avs_writedata.value = 8
        dut.avs_write.value = 1
        await RisingEdge(dut.clk)
        dut.avs_write.value = 0
        
        await RisingEdge(dut.clk)
        assert dut.reg_mode_out.value == 8, "reg_mode_out should reflect mode 8"
    except AttributeError:
        dut._log.error("Port 'reg_mode_out' NOT FOUND in DUT!")
        raise AttributeError("Required port 'reg_mode_out' missing for pipeline sync test")
