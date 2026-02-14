# Burst Master DMA: Trial, Error, and Success
[**English**] | [**한국어**](./BURST_DMA_kor.md)
[⬅️ Back to README](../README.md)

This document records the technical challenges and evolution of the DDR3 connectivity strategy for the DE10-Nano video processing project.

## 1. The Initial Problem: The "DDR Hang"
When the Nios II or Burst Master attempted to read from DDR3 via the **FPGA-to-SDRAM Bridge (Port 0)**, the entire Avalon bus would hang. JTAG UART would stop responding, and the Nios II processor would freeze.

### 🔍 Diagnosis & Verification
- **Register Inspection**: The HPS `sysmgr.f2s_port_en` register controls which FPGA-to-SDRAM ports are enabled.
    - **Expected Value**: `0x01` for Port 0, `0x02` for Port 1, `0x07` for all three.
    - **Finding**: Our Preloader (U-Boot SPL) fixed this at `0x02` (Port 1 only). Since we used Port 0, it hung!

#### How to check the Port Status
1. **In U-Boot**:
   ```bash
   # Read 1 word from f2s_port_en register (0xffd08040)
   md 0xffd08040 1
   ```
2. **In Linux**:
   ```bash
   # Using devmem2 utility
   devmem2 0xffd08040
   ```

---

### 🛠️ Boot Configuration: Boot Arguments (bootargs)
Memory reservation like `mem=512M` is critical for ensuring the HPS doesn't overwrite our video buffers.

#### 1. In U-Boot (Interactive)
If you're at the U-Boot prompt, you can check and set variables directly:
- **Check**: `printenv bootargs`
- **Set**: `setenv bootargs 'console=ttyS0,115200 mem=512M root=${mmcroot} rw rootwait'`
- **Save**: `saveenv` (Permanent change)

#### 2. In Linux (Static)
The bootloader usually reads `uEnv.txt` from the FAT partition of the SD card.
- **Location**: /mnt/boot/uEnv.txt (or similar)
- **Content**: Look for a line starting with `mmcbootargs` or `bootargs`.
- **Check current args**: `cat /proc/cmdline`

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

---

## Appendix: HPS FPGA-to-SDRAM Bridge Global Fix (Linux)

In many cases, the FPGA-to-SDRAM bridge ports are disabled or held in reset by the bootloader (Preloader/U-Boot) for system stability. If your DMA hangs even after proper Qsys configuration, you can use this Linux C program to forcefully release the port resets.

### [Fix Code] bridge_fix.c
```c
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define REG_BASE 0xFFC20000     // SDR Controller Base
#define REG_SPAN 0x10000
#define RESET_REG_OFFSET 0x5080 // fpgaportrst register
#define PORT_EN_OFFSET 0x505C   // f2s_port_en register

int main() {
  int fd;
  void *map_base;
  volatile unsigned int *reset_reg;
  volatile unsigned int *port_en_reg;

  fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (fd < 0) {
    perror("open");
    return 1;
  }

  map_base = mmap(NULL, REG_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, REG_BASE);
  if (map_base == MAP_FAILED) {
    perror("mmap");
    close(fd);
    return 1;
  }

  reset_reg = (volatile unsigned int *)(map_base + RESET_REG_OFFSET);
  port_en_reg = (volatile unsigned int *)(map_base + PORT_EN_OFFSET);

  printf("--- HPS FPGA-to-SDRAM Bridge Global Fix ---\n");
  printf("Current Reset Register: 0x%08X\n", *reset_reg);
  printf("Current Port Enable  : 0x%08X\n", *port_en_reg);

  // 1. Release reset for all ports (Write 0 to fpgaportrst)
  if (*reset_reg != 0) {
    printf("Releasing all FPGA-to-SDRAM port resets...\n");
    *reset_reg = 0x00000000;
  }

  // 2. Report Status
  printf("\n--- Updated Status ---\n");
  printf("New Reset Register: 0x%08X\n", *reset_reg);
  printf("New Port Enable  : 0x%08X\n", *port_en_reg);

  if ((*port_en_reg & 0x02) && (*reset_reg == 0)) {
    printf("\n[SUCCESS] Port 1 (f2h_sdram1) is OPEN and ready!\n");
  } else {
    printf("\n[WARNING] Check bootloader f2s_port_en settings.\n");
  }

  munmap(map_base, REG_SPAN);
  close(fd);
  return 0;
}
```

### Key Explanation

#### 1. SDR Controller Base (`0xFFC20000`)
High-level register base for the HPS SDRAM controller. Accessible via `/dev/mem` (requires root). Reference: *Cyclone V HPS TRM*, "SDRAM Controller Register Map".

#### 2. `fpgaportrst` (Offset `0x5080`)
Controls the hardware reset signal for each FPGA-to-HPS SDRAM port. (1=Reset, 0=Release)

| Bits | Name | Description |
| :--- | :--- | :--- |
| **0 - 5** | **cmd_port_rst** | Reset for **Command Ports 0-5** |
| **6 - 9** | **rd_port_rst** | Reset for **Read Ports 0-3** |
| **10 - 13** | **wr_port_rst** | Reset for **Write Ports 0-3** |
| **14 - 31** | **Reserved** | - |

*Our code writes `0x00000000` to release all 14 port-related resets at once.*

#### 3. `f2s_port_en` (Offset `0x505C` / `staticcfg`)
Static configuration and visibility for the FPGA-to-SDRAM bridge.

| Bit | Name | Description |
| :--- | :--- | :--- |
| **0** | **applycfg** | **Apply Settings**: Set to 1 to commit changes. |
| **1** | **f2s_port0_en** | Enable Port 0 (`f2h_sdram0`) |
| **2** | **f2s_port1_en** | Enable Port 1 (`f2h_sdram1`) |
| **3** | **f2s_port2_en** | Enable Port 2 (`f2h_sdram2`) |
| **4** | **f2s_port3_en** | Enable Port 3 |
| **5** | **f2s_port4_en** | Enable Port 4 |
| **6** | **f2s_port5_en** | Enable Port 5 |

*Note: Bit 2 corresponds to Port 1. That's why we check `(*port_en_reg & 0x02)` to verify `f2h_sdram1` is enabled.*

> [!IMPORTANT]
> **Wait, do we need this for our current AXI Bridge setup?**
> Technically, **No.** Our successful Trial 2 used the **FPGA-to-HPS AXI Slave Bridge**, which bypasses these specific SDRAM port controls. This is exactly why Trial 2 worked immediately!
>
> **Then why keep this?**
> 1.  **Trial 1 Post-Mortem**: It explains exactly why our first attempt (using Port 0) hung.
> 2.  **Performance Tuning**: The dedicated SDRAM ports (Port 0-5) offer lower latency than the AXI Bridge. If you ever need to squeeze out every drop of DDR3 performance at work, you'll need these ports—and this fix!
