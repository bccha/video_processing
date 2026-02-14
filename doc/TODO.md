# TODO: Advanced Video Processing Roadmap
[Back to README](../README.md)

This roadmap outlines the steps to build a high-performance video pipeline, from basic color bars to advanced real-time image processing.

## Phase 1: Foundation (Nios II Pattern & DMA) ✅
- [x] **DDR3 Pattern Generation**: Write Nios II code to fill DDR3 (0x2000_0000) with 720p color bar.
- [x] **Hardware DMA Master**: Implement and verify `burst_master` for high-speed DDR3 access.
- [x] **Performance Benchmarking**: Verify 500x speedup compared to software copy.

## Phase 2: Hardware Extension (RTL Sync Gen & Advanced Control) ✅
- [x] **Custom Sync Gen**: Implement `hdmi_sync_gen.v` with H/V sync and DE.
- [x] **RTL Patterns**: Add built-in patterns (Grid, Grayscale, Character Tile) to the Sync Gen.
- [x] **Advanced Gamma Correction**: Implement sRGB and Inverse Gamma 2.2 LUTs.
- [x] **Nios II Sub-menu**: Create a nested menu for real-time Gamma and Pattern control.
- [x] **Timing & Addressing Fix**: Resolve SDC timing violations and Avalon-MM address mapping bugs.
- [x] **Dynamic Coloring**: Implement coordinate-based rainbow effects for character rendering.

## Phase 3: qHD Video Output ✅
- [x] **Resolution Optimization**: Downgrade from 720p to qHD (960×540@60Hz) for bandwidth compliance.
- [x] **Dual-Clock Architecture**: Separate CSR (50MHz) and Pixel (37.8MHz) clock domains with CDC.
- [x] **Static Image Display**: Nios II loads and displays images from DDR3 via DMA.
- [x] **Linux Video Player**: HPS streams video using double-buffered `/dev/mem` access.
- [x] **V-Sync Synchronization**: Frame pointer latching for tear-free updates.
- [x] **Qsys HPS Bridge**: Connect `h2f_lw_axi_master` to HDMI CSR for Linux control.

## Phase 4: Video Playback Optimization & Bandwidth Expansion ⏳
- [ ] **Bus Width Expansion**: Increase from 4-byte to 8/16-byte bus (target: 400 MB/s @ 50MHz)
  - Goal: Enable 720p@60Hz (222 MB/s) with headroom
  - Keep clock frequency constant (50 MHz)
  - Modify Avalon-MM interface width in burst_master
- [ ] **RAM Preload Mode**: Restore preload strategy for 60fps on short videos (4-5 sec).
- [ ] **Resolution Scaling**: Add 480p/360p modes for sustained SD card streaming.
- [ ] **Video Compression Support**: Integrate H.264/MJPEG hardware decoder.
- [ ] **Audio Integration**: Add I2S audio playback synchronized with video.
- [ ] **Performance Profiling**: Measure and optimize read latency with `ftrace`.

## Phase 5: Real-time Processing (Line Buffer & Filters)
- [ ] **Line Buffer Design**: Implement dual-port RAM based line buffers for 3×3 windowing.
- [ ] **Processing Core**: Implement `video_processing_core.v`.
    - [ ] **Grayscale/Thresholding**: Basic pixel-wise processing.
    - [ ] **Sobel Edge Detection**: High-speed spatial filtering using the line buffers.
    - [ ] **Gaussian Blur**: Smoothing filter for noise reduction.
- [ ] **Real-time Toggle**: Switch between processed and raw video via control register.

## Phase 6: Advanced Features
- [ ] **Spatial Dithering**: Implement Bayer Matrix based dithering to reduce banding.
- [ ] **Linux DRM/KMS Integration**: Map the video pipeline as a standard Linux display device.
- [ ] **Camera Input**: Add MIPI CSI-2 camera interface for live processing.
- [ ] **AI Acceleration**: Integrate hardware-based AI recognition core (YOLO, etc.).
- [ ] **Multi-stream**: Support multiple video sources with hardware compositing.

## Hardware/Qsys Requirements (Common) ✅
- [x] **Clocking**: 37.83 MHz Pixel Clock PLL + SDC Constraints.
- [x] **I2C Control**: ADV7513 initialization via Nios II.
- [x] **Top-level Wiring**: HDMI_TX pins assignment in `DE10_NANO_SoC_GHRD.v`.
- [x] **HPS Bridge**: Lightweight AXI bridge for Linux CSR access.

## Known Issues & Limitations
- **SD Card Bottleneck**: Raw video requires 124 MB/s, SD card provides ~20 MB/s
  - Impact: Sustained playback limited to ~10-15 fps
  - Workaround: Use RAM preload for short clips or lower resolution
- **Memory Constraint**: 512MB DDR3 reserved for video limits preload to ~250 frames
- **No Audio**: Current implementation is video-only

## Documentation Status
- [x] VIDEO_PLAYBACK.md created with comprehensive implementation details
- [x] README.md updated with qHD achievements
- [ ] Create performance benchmark document with detailed measurements
- [ ] Add troubleshooting guide for common issues
