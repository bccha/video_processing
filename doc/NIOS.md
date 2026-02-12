# Nios II Interactive Menu System

This document describes the structure and functionality of the interactive console menu used for controlling the HDMI video pipeline.

## 📌 Overview
The application provides a JTAG UART-based interactive menu that allows users to perform DMA performance tests, initialize hardware, and control RTL pattern generators in real-time.

## 🌳 Menu Tree Structure

The menu is structured hierarchically to manage increasing system complexity.

### 1. Main Menu
The top-level menu handles system-wide tests and hardware initialization.

- **[1] DMA Test (OCM to DDR3)**: Benchmarks 4KB data movement.
- **[2] Burst Test (DDR3 to DDR3)**: Benchmarks 1MB data movement with pipeline processing.
- **[3] Initialize HDMI**: Configures ADV7513 via I2C at 720p.
- **[4] Generate Color Bar**: Writes a test pattern into DDR3 frame buffer.
- **[5] Change RTL Pattern**: Sub-menu for internal RTL pattern generation (Red, Green, Blue, etc.).
- **[6] Gamma Correction Settings**: **[New]** Nested sub-menu for LUT and Toggle control.
- **[C] Load Custom Character**: Uploads a 16x16 bitmap for tile rendering.
- **[r] Reset RTL**: Returns the pattern generator to default state.
- **[q] Quit**: Terminates the application.

---

### 2. Gamma Correction Sub-menu (Nested)
Accessible via option `[6]`, this menu manages hardware Look-Up Table (LUT) settings.

- **[1] Toggle Enable**: Real-time ON/OFF toggle of the Gamma hardware block.
- **[2] Load Gamma 2.2**: Standard Power-law LUT for typical displays.
- **[3] Load sRGB Gamma**: Piecewise linear/power function for improved dark tone detail.
- **[4] Load Inverse Gamma 2.2**: Specialized LUT for linear panels to prevent "washed-out" blacks.
- **[b] Back**: Returns to the Main Menu.

## 📝 Menu Sample (Actual Execution Log)

```text
DE10-Nano Video/DMA Test Environment Initialized
Checking Timer... Timer OK! (Delta=161197)
Initializing Span Extender to 0x20000000... Done.

========== DE10-Nano HDMI Pipeline Menu ==========
 [1] Perform OCM-to-DDR DMA Test (4KB)
 [2] Perform DDR-to-DDR Burst Master Test (1MB)
 [3] Initialize HDMI (ADV7513 via I2C)
 [4] Generate 720p Color Bar Pattern in DDR3
 [5] Change RTL Test Pattern (Red, Green, Blue, etc.)
 [6] Gamma Correction Settings (Table, Toggle, Standard)
 [C] Load Custom Character Bitmap
 [r] Reset RTL Pattern Generator
 [q] Quit
--------------------------------------------------
Select an option: 1

--- [TEST 1] OCM to DDR DMA (burst_master_0) ---
Starting SW Copy (4KB x 100)... Done (4179649 cycles, ~4.6 MB/s)
Starting HW DMA (4KB x 100)... Done (167027 cycles, ~116.9 MB/s)
Speedup: 25 x
SUCCESS: OCM to DDR Verified!

Select an option: 6

--- Gamma Correction Settings ---
 [1] Toggle Enable (Current: OFF)
 [2] Load Gamma 2.2 (Standard)
 [3] Load sRGB Gamma (Standard)
 [4] Load Inverse Gamma 2.2 (for Linear Panel)
 [b] Back to Main Menu
Enter choice: 1
Gamma Correction Enabled
```

---
> [!TIP]
> Use the JTAG UART terminal (nios2-terminal) to interact with the system. All inputs are case-insensitive and processed immediately.
