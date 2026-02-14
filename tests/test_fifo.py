import os
import sys
from cocotb_test.simulator import run

def test_fifo():
    tests_dir = os.path.dirname(os.path.abspath(__file__))
    proj_dir = os.path.dirname(tests_dir)
    rtl_dir = os.path.join(proj_dir, "RTL")
    
    run(
        verilog_sources=[
            os.path.join(rtl_dir, "simple_dcfifo.v")
        ],
        toplevel="simple_dcfifo",
        module="tb_fifo",
        python_search=[
            os.path.join(tests_dir, "cocotb")
        ],
        sim="iverilog",
        force_compile=True
    )

if __name__ == "__main__":
    test_fifo()
