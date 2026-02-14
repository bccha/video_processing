# Video Processing Pipeline - Test Results

[⬅️ Back to README](../README.md)

This document records the performance benchmarks and hardware verification results for the DE10-Nano video processing pipeline.

## 1. DMA Performance Benchmarks

### Burst Master Performance (2026-02-12)

| Test Case | Size | Software (cycles) | Hardware (cycles) | MB/s (HW) | Speedup |
| :--- | :--- | :--- | :--- | :--- | :--- |
| OCM to DDR | 4KB x 100 | 4,185,427 | 166,211 | 117.5 | **25 x** |
| DDR to DDR | 1MB | 207,071,817 | 393,942 | 126.9 | **525 x** |

> [!NOTE]
> DMA (Burst Master 4) significantly offloads the CPU, providing over 500x speedup for 1MB transfers.

## 2. Video Output Verification

### 540p (qHD) Implementation (2026-02-14)

**Resolution:** 960×540 @ 60Hz  
**Pixel Clock:** 37.8336 MHz  
**Bandwidth Required:** 124 MB/s (62% of 50MHz bus capacity)

#### ✅ Verified Features

| Feature | Status | Details |
|---------|--------|---------|
| **Static Image Display** | ✅ Pass | Nios II successfully loads and displays images from DDR3 |
| **Video Playback (Linux)** | ✅ Pass | HPS double-buffered streaming via `/dev/mem` |
| **V-Sync Synchronization** | ✅ Pass | Tear-free frame pointer latching confirmed |
| **Gamma Correction** | ✅ Pass | sRGB and Inverse Gamma 2.2 LUTs working correctly |
| **Pattern Generation** | ✅ Pass | All 8 modes (Color, Grid, Character Tile, etc.) |
| **Dual-Clock CDC** | ✅ Pass | CSR (50MHz) and Pixel (37.8MHz) domains stable |

#### Performance Notes

- **Initial Playback:** 60fps sustained (Linux page cache active)
- **Sustained Playback:** 10-15fps (SD card bottleneck: ~20 MB/s vs 124 MB/s required)
- **RAM Preload Mode (New):** ✅ **60fps Stable** (Duration limit: ~4.1s)
- **Frame Buffer Size:** 2,073,600 bytes (~2MB per frame)
- **Memory Layout:** Reserved Base @ 0x20000000 (512MB Capacity)

## 3. Hardware Initialization Status

### Current Configuration

- **HDMI PLL**: Locked at 37.8336 MHz (540p60)
- **ADV7513 IC**: Configured via I2C successfully
- **Memory Map**: Frame buffers at 0x30000000 (512MB reserved)
- **HPS Bridge**: LWHPS2FPGA connected to HDMI CSR @ 0xFF240000

### Qsys Connectivity

```
hps_0.h2f_lw_axi_master → mm_bridge_0.s0 → hdmi_sync_mm.s0 (Base: 0x40000)
```

## 4. Execution Logs

### DMA Benchmark Log

```text
--- [TEST 1] OCM to DDR DMA (burst_master_0) ---
Starting SW Copy (4KB x 100)... Done (4185427 cycles, ~4.6 MB/s)
Starting HW DMA (4KB x 100)... Done (166211 cycles, ~117.5 MB/s)
Speedup: 25 x
SUCCESS: OCM to DDR Verified!

--- [TEST 2] DDR to DDR DMA (Burst Master 4) ---
Transfer Size: 1 MB
Initializing DDR3 data... Done.
Starting SW Copy (1MB)... Done (207071817 cycles, ~0.2 MB/s)
Starting HW DMA (1MB)... Done (393942 cycles, ~126.9 MB/s)
Speedup: 525 x
Verifying HW Output...
SUCCESS: DDR to DDR Verified! (Coeff=800)
```

### HDMI Initialization Log

```text
Waiting for PLL Lock (37.83 MHz)...
PLL Locked! Initializing ADV7513 HDMI Transmitter...
HDMI Controller Configured. Ready for Video!

Generating 540p Pattern in DDR3... Done! (Total 518400 pixels written)
```

### Video Playback Log (Linux)

```text
DE10-Nano Linux Video Player (Double Buffered / RAM Preload)
Video: video_qhd.bin (960x540)
Mapped Frame Buffers:
  Buffer A (Virtual): 0xb6f00000 (Physical: 0x30000000)
  Buffer B (Virtual): 0xb7100000 (Physical: 0x30200000)
Mapped CSR Base: 0xb6e00000
Started Playback (Double Buffering)...
.........
```

## 5. Advanced Features Validation

### Gamma Correction ✅

- **Mode 7 (Character Tile)**: Confirmed dynamic rainbow coloring effect
- **Gamma LUT Loading**: sRGB and Inverse Gamma 2.2 verified
- **Real-time Toggle**: Gamma enable/disable via CSR working

### Timing Analysis ✅

- **Setup Slack**: Positive (no violations)
- **Hold Slack**: Positive (no violations)
- **Clock Domain Crossing**: Properly constrained via SDC
- **V-Sync Latching**: Shadow pointer updates confirmed on rising edge

## 6. Known Limitations

- **SD Card Bandwidth**: Sustained playback limited to ~10-15fps
  - Required: 124 MB/s
  - Available: ~20 MB/s
- **Memory Constraint**: 512MB DDR3 reserved (max ~250 frames for preload)
- **No Audio**: Video-only implementation

## 7. Next Steps

### Phase 4: Bandwidth Expansion

**Target:** Enable 720p@60Hz (222 MB/s requirement)

**Approach:**
- Expand bus width from 4-byte to 8/16-byte
- Keep clock frequency constant (50 MHz)
- Target bandwidth: 400 MB/s (8-byte @ 50MHz)

**Benefits:**
- 720p@60Hz with 80% headroom
- Improved performance margin
- Future-proof for higher resolutions

---

*Last Updated: 2026-02-14*
