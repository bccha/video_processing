import os
import sys
from cocotb_test.simulator import run

def test_st2110_alignment_wrapper():
    tests_dir = os.path.dirname(os.path.abspath(__file__))
    proj_dir = os.path.dirname(tests_dir)
    rtl_dir = os.path.join(proj_dir, "RTL")
    
    run(
        verilog_sources=[
            os.path.join(rtl_dir, "st2110_alignment_wrapper.v")
        ],
        toplevel="st2110_alignment_wrapper",
        module="tb_st2110_alignment_wrapper",
        python_search=[
            os.path.join(tests_dir, "cocotb")
        ],
        sim="iverilog",
        force_compile=True
    )

if __name__ == "__main__":
    test_st2110_alignment_wrapper()
