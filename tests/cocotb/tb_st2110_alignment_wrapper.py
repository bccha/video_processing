import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer

class AlignmentWrapperDriver:
    def __init__(self, dut, clock):
        self.dut = dut
        self.clock = clock
        self.dut.asi_valid.value = 0
        self.dut.asi_startofpacket.value = 0
        self.dut.asi_endofpacket.value = 0
        self.dut.aso_ready.value = 1

    async def send_payload(self, data_bytes):
        # The wrapper receives 8-bit stream
        # Requires an SOP on the first byte and EOP on the last byte
        for i, b in enumerate(data_bytes):
            self.dut.asi_valid.value = 1
            self.dut.asi_data.value = b
            self.dut.asi_startofpacket.value = 1 if i == 0 else 0
            self.dut.asi_endofpacket.value = 1 if i == len(data_bytes) - 1 else 0
            
            await RisingEdge(self.clock)
            # Wait until ready
            while self.dut.asi_ready.value == 0:
                await RisingEdge(self.clock)
                
        self.dut.asi_valid.value = 0
        self.dut.asi_startofpacket.value = 0
        self.dut.asi_endofpacket.value = 0

@cocotb.test()
async def test_alignment_wrapper(dut):
    """Test ST2110 Alignment Wrapper packs 3 bytes (RGB) + 1 Dummy into 32-bit correctly"""
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start()) # 50MHz
    
    dut.reset_n.value = 0
    await Timer(50, units="ns")
    dut.reset_n.value = 1
    await RisingEdge(dut.clk)
    
    driver = AlignmentWrapperDriver(dut, dut.clk)
    
    # 3 bytes per pixel * e.g. 10 pixels = 30 bytes
    num_pixels = 10
    test_data = []
    for p in range(num_pixels):
        test_data.extend([min(p+1, 255), min(p+2, 255), min(p+3, 255)]) # R, G, B
        
    received_words = []
    sop_indices = []
    eop_indices = []

    async def monitor_output():
        while True:
            await RisingEdge(dut.clk)
            if dut.aso_valid.value == 1 and dut.aso_ready.value == 1:
                received_words.append(int(dut.aso_data.value))
                if dut.aso_startofpacket.value == 1:
                    sop_indices.append(len(received_words) - 1)
                if dut.aso_endofpacket.value == 1:
                    eop_indices.append(len(received_words) - 1)

    cocotb.start_soon(monitor_output())
    
    await driver.send_payload(test_data)
    
    # Let pipeline empty
    for _ in range(10):
        await RisingEdge(dut.clk)
        
    assert len(received_words) == num_pixels, f"Expected {num_pixels} 32-bit words, got {len(received_words)}"
    
    # Check SOP/EOP
    assert len(sop_indices) == 1 and sop_indices[0] == 0, "SOP should be on first word"
    assert len(eop_indices) == 1 and eop_indices[0] == num_pixels - 1, "EOP should be on last word"

    # Check alignment: should be [0x00, R, G, B]
    # In Verilog:
    # shift_reg[23:16] <= R, 15:8 <= G, 7:0 <= B
    # so data is 0x00RRGGBB
    for p in range(num_pixels):
        expected_r = min(p+1, 255)
        expected_g = min(p+2, 255)
        expected_b = min(p+3, 255)
        expected_word = (expected_r << 16) | (expected_g << 8) | expected_b
        
        assert received_words[p] == expected_word, f"Pixel {p}: Expected 0x{expected_word:08X}, got 0x{received_words[p]:08X}"

    dut._log.info("Test passed: 3x8-bit aligned into 32-bit correctly")
