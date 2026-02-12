# TODO: Advanced Video Processing Roadmap

This roadmap outlines the steps to build a high-performance video pipeline, from basic color bars to advanced real-time image processing.

## Phase 1: Foundation (Nios II Pattern & DMA) [/]
- [x] **DDR3 Pattern Generation**: Write Nios II code to fill DDR3 (0x2000_0000) with 720p color bar.
- [ ] **Video DMA (MM2ST Integration)**: Connect/Verify the hardware DMA that sends DDR3 pixels to HDMI.
- [ ] **Basic HDMI Output**: Verify the first stable image on a monitor.

## Phase 2: Hardware Extension (RTL Sync Gen & Menu)
- [ ] **Custom Sync Gen**: Implement `hdmi_sync_gen.v` with H/V sync and DE.
- [ ] **RTL Patterns**: Add built-in patterns (Grid, Moving Square) to the Sync Gen.
- [ ] **Software Control**: Update Nios II menu to switch between DMA and RTL patterns.

## Phase 3: Advanced Processing (Line Buffer & Filters)
- [ ] **Line Buffer Design**: Implement dual-port RAM based line buffers for 3x3 windowing.
- [ ] **Processing Core**: Implement `video_processing_core.v`.
    - [ ] **Grayscale/Thresholding**: Basic pixel-wise processing.
    - [ ] **Sobel Edge Detection**: High-speed spatial filtering using the line buffers.

## Phase 4: High-End Quality (Hybrid Dithering)
- [ ] **Spatial Dithering**: Implement Bayer Matrix based dithering to reduce banding.
- [ ] **Temporal Dithering (FRC)**: Implement frame-rate control for 10-bit color simulation.
- [ ] **Final Integration**: Combine Sobel + Dithering for a professional video output.

## Phase 5: Next-Gen Integration (AI & Linux System)
- [ ] **Linux Frame Buffer (fbdev/DRM)**: Map the video pipeline as a standard Linux display device.
- [ ] **AI Acceleration**: Implement a hardware-based Object Detection core (CNN/YOLO-tiny).
- [ ] **System-on-Chip Harmony**: Stream Linux video to HDMI while performing real-time AI recognition.

## Hardware/Qsys Requirements (Common)
- [ ] **Clocking**: 74.25 MHz Pixel Clock PLL + Reconfig IP.
- [ ] **I2C Control**: ADV7513 initialization via Nios II.
- [ ] **Top-level Wiring**: HDMI_TX pins assignment in `DE10_NANO_SoC_GHRD.v`.
