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

## 📝 Menu Sample
*(User-provided sample will be placed here)*

---
> [!TIP]
> Use the JTAG UART terminal (nios2-terminal) to interact with the system. All inputs are case-insensitive and processed immediately.
