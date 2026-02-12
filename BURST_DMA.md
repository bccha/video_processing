# Burst Master DMA: Trial, Error, and Success

This document records the technical challenges and evolution of the DDR3 connectivity strategy for the DE10-Nano video processing project.

## 1. The Initial Problem: The "DDR Hang"
When the Nios II or Burst Master attempted to read from DDR3 via the **FPGA-to-SDRAM Bridge (Port 0)**, the entire Avalon bus would hang. JTAG UART would stop responding, and the Nios II processor would freeze.

### Diagnosis
- **Register Inspection**: Checking HPS `sysmgr.f2s_port_en` revealed it was fixed at `0x02`.
- **Finding**: The Preloader (U-Boot SPL) configured the HPS to only enable **Port 1** (`f2h_sdram1`). Since the design was using Port 0, any access through it failed silently at the bridge level.

---

## 2. Trial 1: Switching to SDRAM Port 1
We modified Qsys to use `f2h_sdram1` instead of `f2h_sdram0`.
- **Result**: Still failed. 
- **Blocker**: The Linux kernel's `fpga_bridge` driver (specifically for `br3`) keeps the bridge in reset unless explicitly enabled. However, bridge control via `/sys/class/fpga_bridge` was locked or inaccessible without root/preloader modification.

---

## 3. Trial 2: Relocating to F2H AXI Bridge (Success!)
We abandoned the dedicated SDRAM ports and switched to the **FPGA-to-HPS AXI Slave Bridge**.
- **Strategy**: Enable the AXI bridge in Qsys and route all DDR3 traffic through the HPS L3 Interconnect.
- **Result**: **SUCCESS**. The AXI bridge is typically initialized and left open by the GHRD environment, providing a reliable bidirectional path to DDR3.

---

## 4. Software Implementation Challenges

### 🛑 Challenge 1: The "Self-Destruct" Bug (OCM Overwrite)
- **Issue**: Initial test code wrote data to `ONCHIP_MEMORY2_0_BASE` (0x0).
- **Error**: Nios II reset vector and code live at address `0x0`. The Burst Master benchmark was overwriting the very instructions Nios II was executing, causing a crash.
- **Fix**: Used a **static global array** (`src_buffer`) and let the linker assign a safe memory location.

### 🛑 Challenge 2: Cache Coherency (Invisible Data)
- **Issue**: Nios II wrote data to OCM, but the Burst Master (Hardware) read old/random data.
- **Cause**: The data was sitting in the Nios II Data Cache and hadn't been written to the physical OCM RAM yet.
- **Fix**: Added `alt_dcache_flush_all()` before triggering the DMA operation.

### 🛑 Challenge 3: Memory Capacity Limits
- **Issue**: A 64KB test buffer caused a linker error: `section .bss is not within region onchip_memory2_0`.
- **Cause**: The DE10-Nano GHRD OCM is ~100KB. Code + Stack + 64KB Buffer was too much.
- **Fix**: Reduced buffer to **4KB** and repeated the test **100 times** to maintain timing accuracy.

---

## 5. Final Results & Benchmarks

The transition to hardware-driven DMA resulted in a massive performance leap. By repeating the 4KB transfer **100 times**, we obtained stable metrics for both software and hardware paths.

### 📊 Comparative Analysis
| Method | Total Data | Time (ms) | Throughput (MB/s) |
| :--- | :--- | :--- | :--- |
| **Software Copy** (CPU Loop) | 400 KB | 90.00 ms | 4.55 MB/s |
| **Burst Master (DMA)** | **400 KB** | **3.00 ms** | **136.53 MB/s** |

### Why is `burst_master` so much faster?
1.  **Burst Transfers**: Standard Nios II I/O instructions perform single-beat transactions (Address -> Data). The `burst_master` sends **one address** and then pulls/pushes **up to 64 data words** continuously, maximizing bus utilization.
2.  **Dedicated Hardware**: While Nios II is busy fetching instructions and managing the loop counter, the `burst_master` is purely data-driven, leveraging its internal FIFO for buffering and pipelining.
3.  **AXI Bridge Efficiency**: The FPGA-to-HPS AXI bridge is optimized for high-throughput bursts, allowing the hardware master to reach DDR3 with minimal latency compared to the software master's single-beat access through the same path.

### 📝 Final Verification Log
```text
--- Burst Master Performance Test (100 Iterations) ---
Unit Size: 4 KB
Total Size: 400 KB (100 iterations)
Step 1: Running Software Copy (100x)...
  -> SW Time: 90.00 ms (Throughput: 4.55 MB/s)
Step 2: Running Hardware DMA (100x)...
  -> HW Time: 3.00 ms (Throughput: 136.53 MB/s)
Step 3: Verifying HW DMA Integrity...

[SUCCESS] 100 iterations complete! DMA wins! 🎉
```

---

## 6. Phase 2: DDR-to-DDR Pipeline DMA & Memory Protection

Beyond simple data transfers, we measured pixel-processing performance using `burst_master_4` which includes a 4-stage arithmetic pipeline. This test performs a multiplication by a coefficient followed by a division by 400 (`Pixel_Out = (Pixel_In * Coeff) / 400`), which serves as the foundation for video filters and color space conversion algorithms. We also implemented memory protection strategies to avoid HPS (ARM/Linux) system space.

### 🛑 Challenge 4: HPS Memory Conflict (0x0 Address)
- **Problem**: Physical address 0x0 is reserved for the ARM Vector Table and Kernel. Writing to this region via DMA triggers immediate system crashes.
- **Solution**: Shifted all DMA test addresses to the safe region starting at **512MB (0x20000000)**.
- **Implementation**: Initialized the `Address Span Extender` window base to `0x20000000` during startup to ensure alignment between Nios II and the hardware DMA.

### 📊 DDR-to-DDR Benchmark (1 MB)
| Method | Transfer Size | Time | Throughput | Speedup |
| :--- | :--- | :--- | :--- | :--- |
| **Software Copy** (Division) | 1 MB | 4.683 s | 0.21 MB/s | Baseline |
| **Hardware DMA** (4-Stage) | **1 MB** | **0.008 s** | **125.00 MB/s** | **~585x** |

### 📝 Final Verification Log
```text
Setting Span Extender window to 0x20000000... Done.
--- Test 2: DDR to DDR DMA (1MB, Coeff=800) ---
  -> SW Time: 4.683 s, Rate: 0.21 MB/s
  -> HW Time: 0.008 s, Rate: 125.00 MB/s
  -> Speedup: 585.38x
[SUCCESS] HW DMA results match SW reference! 🎉
```

## 7. Conclusion
The combination of the **AXI Bridge** and **Burst Master DMA** is the most stable and high-performance method for utilizing DDR3 resources on the DE10-Nano. The verified throughput of 125 MB/s is sufficient for real-time 720p HD video streaming, and the successful integration with an arithmetic pipeline proves its readiness for advanced image processing tasks.
