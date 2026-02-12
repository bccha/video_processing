# TODO: HDMI Color Bar Display Roadmap

This roadmap outlines the steps to display a color bar pattern on an HDMI monitor by fetching data from DDR3 memory using the verified DMA pipeline.

## 1. Platform Designer (Qsys) Integration
- [ ] **Pixel Clock PLL**: Add `Altera PLL` to generate 74.25 MHz (720p).
- [ ] **PLL Reconfig**: Add `Altera PLL Reconfig` to allow Nios II to tune the clock.
- [ ] **I2C Master**: Add `OpenCores I2C` or `Parallel to I2C` for ADV7513 configuration.
- [ ] **Video DMA**: Custom Avalon-MM Master connected directly to the F2H AXI Bridge.
- [ ] **Video Pipeline**: 
    - [ ] Add `Video FIFO` for clock domain crossing and buffering.
    - [ ] **Custom Sync Generator**: Create Verilog module for HSync/VSync/DE.
    - [ ] Implement RGB data output logic synchronized with timing signals.

## 2. FPGA Top-level RTL (Verilog)
- [ ] **Pin Mapping**: Connect Qsys HDMI signals to physical DE10-Nano pins:
    - `HDMI_TX_D[23:0]`
    - `HDMI_TX_HS` / `HDMI_TX_VS` / `HDMI_TX_DE`
    - `HDMI_TX_CLK`
- [ ] **I2C Pins**: Connect `HDMI_I2C_SDA` and `HDMI_I2C_SCL` (with pull-up logic if needed).

## 3. Nios II Control Software
- [ ] **I2C Driver**: Implement initialization for ADV7513 (Set output format, Power on).
- [ ] **PLL Driver**: Implement code to wait for PLL Lock and handle reconfiguration.
- [ ] **DMA Launcher**: Initialize the DMA to fetch from the reserved DDR3 space (0x20000000).

## 4. Verification (The Color Bar)
- [ ] **Pattern Setup**: Write a software loop in Nios II to fill DDR3 with a 1280x720 RGB color bar pattern.
- [ ] **Final Run**: Start the pipeline and verify output on a 720p monitor.
