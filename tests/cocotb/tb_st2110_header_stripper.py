import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer

class HeaderStripperDriver:
    def __init__(self, dut, clock):
        self.dut = dut
        self.clock = clock
        self.dut.asi_valid.value = 0
        self.dut.aso_ready.value = 1

    async def send_packet(self, data_bytes):
        for i, b in enumerate(data_bytes):
            self.dut.asi_valid.value = 1
            self.dut.asi_data.value = b
            await RisingEdge(self.clock)
            # wait until ready is high
            while self.dut.asi_ready.value == 0:
                await RisingEdge(self.clock)
        
        self.dut.asi_valid.value = 0

@cocotb.test()
async def test_header_stripper(dut):
    """Test ST2110 Header Stripper drops 62 bytes and asserts SOP/EOP correctly"""
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start()) # 50MHz
    
    dut.reset_n.value = 0
    await Timer(50, units="ns")
    dut.reset_n.value = 1
    await RisingEdge(dut.clk)
    
    driver = HeaderStripperDriver(dut, dut.clk)
    
    # We will send a packet of length 1502 bytes (62 header + 1440 payload)
    packet_len = 1502
    # Packet 1: SOP, no EOP
    pkt1 = [i % 256 for i in range(packet_len)]
    pkt1[43] &= ~0x80 # Marker = 0
    pkt1[58] = 0; pkt1[59] = 0 # Line 0
    pkt1[60] = 0; pkt1[61] = 0 # Offset 0
    
    # Packet 2: Normal
    pkt2 = [(i+1) % 256 for i in range(packet_len)]
    pkt2[43] &= ~0x80 # Marker = 0
    pkt2[58] = 0; pkt2[59] = 1 # Line 1
    pkt2[60] = 0; pkt2[61] = 0
    
    # Packet 3: EOP, no SOP
    pkt3 = [(i+2) % 256 for i in range(packet_len)]
    pkt3[43] |= 0x80 # Marker = 1
    pkt3[58] = 0; pkt3[59] = 2 # Line 2
    pkt3[60] = 0; pkt3[61] = 0
    
    # Start checking output
    received_data = []
    sop_indices = []
    eop_indices = []
    
    async def monitor_output():
        while True:
            await RisingEdge(dut.clk)
            if dut.aso_valid.value == 1 and dut.aso_ready.value == 1:
                received_data.append(int(dut.aso_data.value))
                if dut.aso_startofpacket.value == 1:
                    sop_indices.append(len(received_data) - 1)
                if dut.aso_endofpacket.value == 1:
                    eop_indices.append(len(received_data) - 1)

    cocotb.start_soon(monitor_output())
    
    await driver.send_packet(pkt1)
    
    # Small gap between packets to test robustness
    for _ in range(5): await RisingEdge(dut.clk)
    
    await driver.send_packet(pkt2)
    for _ in range(5): await RisingEdge(dut.clk)
        
    await driver.send_packet(pkt3)
    
    # Let pipeline empty
    for _ in range(20):
        await RisingEdge(dut.clk)
        
    expected_payload_length = 1440 * 3
    
    assert len(received_data) == expected_payload_length, f"Expected {expected_payload_length} bytes, got {len(received_data)}"
    assert received_data[0] == pkt1[62], "First byte of pkt1 doesn't match"
    assert received_data[1440] == pkt2[62], "First byte of pkt2 doesn't match"
    assert received_data[2880] == pkt3[62], "First byte of pkt3 doesn't match"
    assert len(sop_indices) == 1, "Should have exactly one SOP across all 3 packets"
    assert sop_indices[0] == 0, "SOP should be on the very first valid byte"
    assert len(eop_indices) == 1, "Should have exactly one EOP across all 3 packets"
    assert eop_indices[0] == expected_payload_length - 1, "EOP should be on the very last valid byte"

    dut._log.info("Test passed: 62 bytes stripped, SOP/EOP indices correct")
