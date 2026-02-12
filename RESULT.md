# Video Processing Pipeline Analysis Results

This document records the performance benchmarks and hardware initialization status of the DE10-Nano video processing pipeline.

## 1. DMA Performance Benchmarks (2026-02-12)

| Test Case | Size | Software (cycles) | Hardware (cycles) | MB/s (HW) | Speedup |
| :--- | :--- | :--- | :--- | :--- | :--- |
| OCM to DDR | 4KB x 100 | 4,185,427 | 166,211 | 117.5 | **25 x** |
| DDR to DDR | 1MB | 207,071,817 | 393,942 | 126.9 | **525 x** |

> [!NOTE]
> DMA (Burst Master 4) significantly offloads the CPU, providing over 500x speedup for 1MB transfers.

## 2. Hardware Initialization Status

- **HDMI PLL**: Locked at 74.25 MHz (720p60 target)
- **ADV7513 IC**: Configured via I2C successfully
- **Memory Map**: Nios II & DMA isolated at 0x20000000 (512MB offset)

## 3. Official Execution Log

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

Waiting for PLL Lock (74.25 MHz)...
PLL Locked! Initializing ADV7513 HDMI Transmitter...
HDMI Controller Configured. Ready for Video!

Generating 720p Color Bar Pattern in DDR3... Done! (Total 921600 pixels written)
```

---
*Created by Nios II Performance Monitoring Unit.*
