import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer, ReadOnly, Join
from cocotb.queue import Queue
import random

class AvalonMemory:
    def __init__(self, dut, size=1024*1024):
        self.dut = dut
        self.mem = {}
        # Populate memory with pattern
        for i in range(0, size, 4):
            self.mem[i] = (i // 4) & 0xFFFFFFFF # Addr/4 pattern
        
        self.req_queue = Queue()
            
    async def run(self):
        """Standard Avalon-MM Slave Responder"""
        # Ensure outputs are driven
        self.dut.m_waitrequest.value = 0
        self.dut.m_readdatavalid.value = 0
        self.dut.m_readdata.value = 0
        
        # Fork response driver
        cocotb.start_soon(self.response_driver())
        
        while True:
            await RisingEdge(self.dut.clk)
            
            # Default
            # self.dut.m_readdatavalid.value = 0 # Driver handles this
            
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
                    cocotb.log.warning("Avalon Master asserted Read with Undefined Address/Burst")
                    continue
                
                # Enqueue Request
                self.req_queue.put_nowait((addr, burst))
                
    async def response_driver(self):
        while True:
            # Get next request
            addr, burst = await self.req_queue.get()
            
            # Latency (simulated memory access time)
            latency = random.randint(5, 20)
            for _ in range(latency):
                await RisingEdge(self.dut.clk)
                self.dut.m_readdatavalid.value = 0
            
            # Send Burst
            for i in range(burst):
                await RisingEdge(self.dut.clk)
                self.dut.m_readdatavalid.value = 1
                addr_cal = addr + (i * 4)
                data = self.mem.get(addr_cal, 0xBADF00D)
                self.dut.m_readdata.value = data
            
            # End Burst
            await RisingEdge(self.dut.clk)
            self.dut.m_readdatavalid.value = 0

@cocotb.test()
async def test_dma_basic_transfer(dut):
    """Test DMA Single Frame Transfer"""
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())
    
    # Initialize ALL Inputs
    dut.reset_n.value = 0
    dut.dma_start.value = 0
    dut.dma_cont_en.value = 0
    dut.fifo_used.value = 0
    dut.m_waitrequest.value = 0
    dut.m_readdata.value = 0
    dut.m_readdatavalid.value = 0
    dut.start_addr.value = 0
    dut.vsync_edge.value = 0
    
    # Memory Model
    mem_model = AvalonMemory(dut)
    cocotb.start_soon(mem_model.run())
    
    await Timer(50, units="ns")
    dut.reset_n.value = 1
    await Timer(50, units="ns")
    
    # Start DMA
    dut.start_addr.value = 0
    dut.dma_start.value = 1
    await RisingEdge(dut.clk)
    dut.dma_start.value = 0
    
    # Monitor FIFO Writes
    expected_count = 0
    last_val = -1
    timeout_counter = 0
    
    # Verify at least 512 words (8 bursts) to ensure multiple bursts work smoothly
    while expected_count < 512: 
        await RisingEdge(dut.clk)
        
        if dut.fifo_wr_en.value == 1:
            try:
                val = int(dut.fifo_wr_data.value)
            except ValueError:
                val = -1
            
            # dut._log.info(f"FIFO Write: {val} (Expected {expected_count})")
            
            if val != expected_count:
                raise AssertionError(f"Data mismatch! Got {val}, expected {expected_count}")
            
            last_val = val
            expected_count += 1
            timeout_counter = 0
        
        timeout_counter += 1
        if timeout_counter > 10000:
             raise TimeoutError(f"DMA did not write data. Current count: {expected_count}")

    dut._log.info(f"Verified {expected_count} words written to FIFO")

@cocotb.test()
async def test_dma_fifo_backpressure(dut):
    """Test DMA pauses when FIFO is full"""
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())
    
    # Initialize Inputs
    dut.reset_n.value = 0
    dut.dma_start.value = 0
    dut.dma_cont_en.value = 0
    dut.start_addr.value = 0
    dut.vsync_edge.value = 0
    dut.fifo_used.value = 0
    
    dut.m_waitrequest.value = 0
    dut.m_readdata.value = 0
    dut.m_readdatavalid.value = 0
    
    mem_model = AvalonMemory(dut)
    cocotb.start_soon(mem_model.run())
    
    await Timer(50, units="ns")
    dut.reset_n.value = 1
    
    # Start DMA
    dut.dma_start.value = 1
    await RisingEdge(dut.clk)
    dut.dma_start.value = 0
    
    # Let it run a bit
    await Timer(500, units="ns")
    
    # Assert FIFO "Almost Full" 
    dut.fifo_used.value = 450
    
    await Timer(2000, units="ns") 
    
    # Monitor for silence on m_read
    read_commands = 0
    for _ in range(200):
        await RisingEdge(dut.clk)
        if dut.m_read.value == 1:
            read_commands += 1
            
    assert read_commands == 0, "DMA issued read commands despite FIFO being almost full!"
    
    # Release backpressure
    dut.fifo_used.value = 0
    await Timer(500, units="ns")
    
    # Should see reads again
    read_commands = 0
    for _ in range(200):
        await RisingEdge(dut.clk)
        if dut.m_read.value == 1:
            read_commands += 1
            
    assert read_commands > 0, "DMA did not resume after FIFO space cleared"
