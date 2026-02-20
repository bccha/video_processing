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

## Phase 3: DMA Video Output (960x540 qHD) [x]
- [x] **MM2ST Video Pipeline**: Integrate the DMA Master with a Stream-to-Video bridge.
- [x] **HPS-to-FPGA Connectivity**: Enable ARM (Linux) to control FPGA CSRs via Lightweight Bridge.
- [x] **Frame Buffer Control**: Implement Linux/Nios II logic to manage double-buffering in DDR3.
- [x] **Stable Video Output**: Verify 540p video stream from DDR3 to HDMI monitor.
- [ ] **Hardware Refinement**: Replace handwritten FIFO with Intel **DCFIFO IP** to resolve frame jitter.

## Phase 4: Real-time Processing (Line Buffer & Filters) [x]
- [x] **Line Buffer Design**: Implement dual-port RAM based line buffers for 3x3 windowing.
- [x] **Processing Core**: Implement pipelined 3x3 image filters (`image_filter.v`).
    - [x] **Grayscale / Bypass**: Basic pixel-wise processing.
    - [x] **Blur Filter**: Averaging 3x3 neighbor pixels.
    - [x] **Sobel Edge Detection**: High-speed spatial filtering using the line buffers.
    - [ ] **Embossing Filter**: Directional difference filtering for 3D depth effect.
    - [ ] **Sharpening Filter**: High-pass filtering to enhance image details.


## Phase 5: High-End Quality & Integration
- [ ] **Spatial Dithering**:
    - [ ] **Ordered Dithering**: Implement Bayer Matrix (2x2, 4x4) to reduce color banding in gradients.
    - [ ] **Error Diffusion**: Explore Floyd-Steinberg algorithm (requires line buffer for error propagation).
- [ ] **Temporal Dithering (FRC)**: 
    - [ ] **Frame Rate Control**: Implement temporal flickering between two intensive levels to increase perceived bit depth.
- [ ] **Linux Integration**: Map the video pipeline as a standard Linux display device (DRM/KMS).
- [ ] **AI Acceleration**: Integrate hardware-based AI recognition core.

## Hardware/Qsys Requirements (Common) [x]
- [x] **Clocking**: 74.25 MHz Pixel Clock PLL + SDC Constraints.
- [x] **I2C Control**: ADV7513 initialization via Nios II.
- [x] **Top-level Wiring**: HDMI_TX pins assignment in `DE10_NANO_SoC_GHRD.v`.
