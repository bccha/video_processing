#**************************************************************
# This .sdc file is created by Terasic Tool.
# Users are recommended to modify this file to match users logic.
#**************************************************************

#**************************************************************
# Create Clock
#**************************************************************
create_clock -period "50.0 MHz" [get_ports FPGA_CLK1_50]
create_clock -period "50.0 MHz" [get_ports FPGA_CLK2_50]
create_clock -period "50.0 MHz" [get_ports FPGA_CLK3_50]

# for enhancing USB BlasterII to be reliable, 25MHz
create_clock -name {altera_reserved_tck} -period 40 {altera_reserved_tck}
set_input_delay -clock altera_reserved_tck -clock_fall 3 [get_ports altera_reserved_tdi]
set_input_delay -clock altera_reserved_tck -clock_fall 3 [get_ports altera_reserved_tms]
set_output_delay -clock altera_reserved_tck 3 [get_ports altera_reserved_tdo]

#**************************************************************
# Create Generated Clock
#**************************************************************
derive_pll_clocks



#**************************************************************
# Set Clock Latency
#**************************************************************



#**************************************************************
# Set Clock Uncertainty
#**************************************************************
derive_clock_uncertainty



#**************************************************************
# Set Input Delay
#**************************************************************



#**************************************************************
# Set Output Delay
#**************************************************************



#**************************************************************
# Set Clock Groups
#**************************************************************

# By defining asynchronous clock groups, we tell the Quartus Fitter/TimeQuest
# NOT to analyze paths CROSSING between these separate clock domains.
# The Video Pipeline, Nios, HPS/DDR, and 50MHz board inputs are separated by
# FIFOs/CDCs in Qsys, so they are truly Asynchronous.
#
# Doing this prevents the Router from wasting hours trying to achieve 
# impossible Timing Closure on CDC boundaries.

set_clock_groups -asynchronous \
    -group [get_clocks {FPGA_CLK1_50 FPGA_CLK2_50 FPGA_CLK3_50}] \
    -group [get_clocks {u0|pll_0|altera_pll_i|*|divclk}] \
    -group [get_clocks {*|hps_0|hps_io|border|h2f_user0_clock}]

# Note: Add additional groups with wildcards for any specific AXI/Avalon 
# Video PLL clocks or Nios core clocks if they differ from the pll_0 divclk.



#**************************************************************
# Set False Path
#**************************************************************



#**************************************************************
# Set Multicycle Path
#**************************************************************



#**************************************************************
# Set Maximum Delay
#**************************************************************



#**************************************************************
# Set Minimum Delay
#**************************************************************



#**************************************************************
# Set Input Transition
#**************************************************************



#**************************************************************
# Set Load
#**************************************************************
