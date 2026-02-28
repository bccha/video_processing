import os
import pytest
from cocotb_test.simulator import run

def test_image_filter():
    tests_dir = os.path.dirname(os.path.abspath(__file__))
    proj_dir = os.path.dirname(tests_dir)
    rtl_dir = os.path.join(proj_dir, "RTL")
    
    # Run simulation for a specific mode
    # 0:Bypass, 1:Gray, 2:BlurG, 3:BlurC, 4:EdgeG, 5:EdgeC, 6:Emboss, 7:Sharpen
    # Let's test Sharpen (Mode 7)
    os.environ['FILTER_MODE'] = '7'
    
    run(
        verilog_sources=[
            os.path.join(rtl_dir, "delay_line.v"),
            os.path.join(rtl_dir, "line_buffer.v"),
            os.path.join(rtl_dir, "filter_blur.v"),
            os.path.join(rtl_dir, "filter_edge.v"),
            os.path.join(rtl_dir, "filter_emboss.v"),
            os.path.join(rtl_dir, "filter_sharpen.v"),
            os.path.join(rtl_dir, "image_filter.v")
        ],
        toplevel="image_filter",
        module="tb_image_filter",
        python_search=[
            os.path.join(tests_dir, "cocotb")
        ],
        sim="iverilog",
        force_compile=True
    )

if __name__ == "__main__":
    test_image_filter()
