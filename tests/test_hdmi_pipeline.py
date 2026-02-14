import os
import sys
from cocotb_test.simulator import run

def test_hdmi_sync_gen():
    tests_dir = os.path.dirname(os.path.abspath(__file__))
    proj_dir = os.path.dirname(tests_dir)
    rtl_dir = os.path.join(proj_dir, "RTL")
    
    # Standard cocotb-test run call
    run(
        verilog_sources=[os.path.join(rtl_dir, "hdmi_sync_gen.v")],
        toplevel="hdmi_sync_gen",
        module="tb_hdmi_sync_gen",
        python_search=[
            os.path.join(tests_dir, "cocotb")
        ],
        sim="iverilog",
        force_compile=True
    )

if __name__ == "__main__":
    test_hdmi_sync_gen()
