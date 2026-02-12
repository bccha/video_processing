# TODO: Advanced Video Processing Roadmap
[Back to README](../README.md)

This roadmap outlines the steps to build a high-performance video pipeline, from basic color bars to advanced real-time image processing.

## Phase 1: Foundation (Nios II Pattern & DMA) [x]
- [x] **DDR3 Pattern Generation**: Write Nios II code to fill DDR3 (0x2000_0000) with 720p color bar.
- [x] **Hardware DMA Master**: Implement and verify `burst_master` for high-speed DDR3 access.
- [x] **Performance Benchmarking**: Verify 500x speedup compared to software copy.

## Phase 2: Hardware Extension (RTL Sync Gen & Advanced Control) [x]
- [x] **Custom Sync Gen**: Implement `hdmi_sync_gen.v` with H/V sync and DE.
- [x] **RTL Patterns**: Add built-in patterns (Grid, Grayscale, Character Tile) to the Sync Gen.
- [x] **Advanced Gamma Correction**: Implement sRGB and Inverse Gamma 2.2 LUTs.
- [x] **Nios II Sub-menu**: Create a nested menu for real-time Gamma and Pattern control.
- [x] **Timing & Addressing Fix**: Resolve SDC timing violations and Avalon-MM address mapping bugs.
- [x] **Dynamic Coloring**: Implement coordinate-based rainbow effects for character rendering.

## Phase 3: DMA Video Output (Next Step) [/]
- [ ] **MM2ST Video Pipeline**: Integrate the DMA Master with a Stream-to-Video bridge.
- [ ] **Frame Buffer Control**: Implement Nios II logic to manage double-buffering in DDR3.
- [ ] **Stable Video Output**: Verify jitter-free 720p video stream from DDR3 to HDMI monitor.

## Phase 4: Real-time Processing (Line Buffer & Filters)
- [ ] **Line Buffer Design**: Implement dual-port RAM based line buffers for 3x3 windowing.
- [ ] **Processing Core**: Implement `video_processing_core.v`.
    - [ ] **Grayscale/Thresholding**: Basic pixel-wise processing.
    - [ ] **Sobel Edge Detection**: High-speed spatial filtering using the line buffers.

## Phase 5: High-End Quality & Integration
- [ ] **Spatial Dithering**: Implement Bayer Matrix based dithering to reduce banding.
- [ ] **Linux Integration**: Map the video pipeline as a standard Linux display device (DRM/KMS).
- [ ] **AI Acceleration**: Integrate hardware-based AI recognition core.

## Hardware/Qsys Requirements (Common) [x]
- [x] **Clocking**: 74.25 MHz Pixel Clock PLL + SDC Constraints.
- [x] **I2C Control**: ADV7513 initialization via Nios II.
- [x] **Top-level Wiring**: HDMI_TX pins assignment in `DE10_NANO_SoC_GHRD.v`.
