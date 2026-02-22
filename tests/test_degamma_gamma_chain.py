import os
from cocotb_test.simulator import run

def test_degamma_gamma_chain():
    tests_dir = os.path.dirname(os.path.abspath(__file__))
    proj_dir  = os.path.dirname(tests_dir)
    rtl_dir   = os.path.join(proj_dir, "RTL")

    run(
        verilog_sources=[
            os.path.join(rtl_dir, "filter_degamma.v"),
            os.path.join(rtl_dir, "filter_color_matrix.v"),
            os.path.join(rtl_dir, "filter_gamma.v"),
        ],
        toplevel="filter_color_matrix",
        module="tb_degamma_gamma_chain",
        python_search=[os.path.join(tests_dir, "cocotb")],
        sim="iverilog",
        force_compile=True
    )

if __name__ == "__main__":
    test_degamma_gamma_chain()
