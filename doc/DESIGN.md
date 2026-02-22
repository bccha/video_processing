# Design Specification: HDMI Video Pipeline
[⬅️ Back to README](../README.md)
 Specification

This document outlines the architecture and technical design for streaming video data from HPS DDR3 memory to the HDMI display interface on the DE10-Nano platform.

## 1. System Architecture & Data Flow

Video data is transferred through a high-bandwidth path to ensure real-time performance:
**SD Card (ARM/Linux) ➡️ DDR3 Memory ➡️ Video DMA (FPGA) ➡️ Filter Pipeline ➡️ HDMI TX (ADV7513)**

```mermaid
graph LR
    subgraph "HPS (ARM Cortex-A9)"
        SD[SD Card Image] --> SW_Load[Image Loader]
        SW_Load --> DDR[DDR3 Memory]
    end

    subgraph "FPGA Pixel Pipeline (37.8 MHz)"
        DDR --> AXI[F2H AXI Bridge]
        AXI --> V_DMA[Video DMA Master]
        V_DMA --> FIFO[Video FIFO]
        FIFO --> SGEN[Custom Sync Gen]
        SGEN --> FLT["Image Filter\n(Blur/Edge/Sharpen)"]
        FLT --> DG["De-Gamma LUT\n(sRGB → Linear)"]
        DG --> CM["3×3 Gamut Matrix\n(Q2.10 fixed-point)"]
        CM --> DTH["Bayer + Temporal Dither\n(post-matrix)"] 
        DTH --> ERR["Floyd-Steinberg\nError Diffusion"]
    end

    subgraph "System Control"
        Nios[Nios II Processor]
        Nios --> I2C[I2C Master]
        Nios --> CM
        I2C -.-> HDMI_Chip[ADV7513 HDMI TX]
    end

    ERR --> HDMI_Chip
```

<img src="./images/design.png" width="50%" alt="System Architecture Diagram">

---

## 2. Component Responsibilities

### HPS (ARM/Linux Core)
- **Data Acquisition**: Transfers sources from the SD card to Linux.
- **RAM Preload**: Implements a preload buffer to overcome SD card bandwidth limits (90MB/s required for 540p@60fps).

### Nios II Processor (Control Layer)
- **Modular Filter Control**: Manages a 4-bit `filter_mode` CSR to switch between 16 possible filter algorithms in real-time.
- **Peripheral Configuration**: Initializes the ADV7513 HDMI Transmitter via I2C.

### FPGA Fabric (High-Speed Data Path)
- **Full-Pipeline Color Processing (5-Stage)**:
    The FPGA implements a fully pipelined color processing chain at 37.8 MHz:

    | Stage | Module | Clocks | Description |
    |-------|--------|--------|-------------|
    | 1 | `image_filter` | 3 | 3×3 spatial filters (8-bit I/O) |
    | 2 | `filter_degamma` | 1 | **[8-to-12 bit]** sRGB → Linear via 256×12-bit LUT |
    | 3 | `filter_color_matrix` | 3 | **[12-bit I/O]** 3×3 gamut transfer, Q2.10, high-precision |
    | 4 | `filter_gamma` | 1 | **[12-to-8 bit]** Linear → Display via 4096×8-bit LUT |
    | 5 | `filter_dither` | 2 | **Post-matrix** Bayer + Temporal dithering (8-bit) |
    | 6 | `filter_error_diffusion` | — | Floyd-Steinberg, 960-word BRAM line buffer |

    > **Why `filter_dither` must come after the gamut matrix for LED displays:**
    > LED panels have a minimum emission threshold — pixels below that level produce no light, introducing non-linearity in the low-luminance region where gamut errors are largest. Placing `filter_dither` **after** the gamut matrix means dithering noise is injected into *already-corrected* values. Even if the target color falls below the LED emission floor, dithering borrows energy from neighboring pixels and frames that *are* above threshold. `filter_error_diffusion` then redistributes residual quantization error spatially. The result is **perceptually accurate gamut reproduction even on panels that cannot directly render low-luminance colors.**

    - **Split-Screen Support**: Real-time split-screen (x < 480) to compare truncated vs. dithered output.
    - **Parallel Filter Computation**: Blur, Edge, Emboss, Sharpen computed in parallel with matched 3-clock pipeline.

- **High-Precision 12-bit Internal Path**:
    To prevent **Low-Gray Banding (Crushing Black)**, the pipeline uses 12-bit precision for linear-space operations.
    - **Why 12-bit?**: In the sRGB de-gamma process, many dark colors (e.g., 0-15) map to nearly zero in 8-bit linear space ($15/255^{2.2} \approx 0.002 \rightarrow 0/255$), losing details before the matrix stage. 
    - **12-bit Benefit**: By expanding to 4096 levels, we preserve the distinct steps of the lowest sRGB gradations ($15/255^{2.2} \times 4095 \approx 8$), ensuring that the 3x3 color matrix can process subtle shadow details without quantization artifacts.

- **Video DMA Master**: Fetches pixel data from DDR3 via Avalon-MM.

---

## 3. Technical Design Choices

1. **Modular Filter Architecture**: Decoupling the filter logic into submodules (`filter_blur`, `filter_edge`, etc.) allows for independent verification and easy expansion of effects.
2. **Fixed-Point Arithmetic**: Used for kernel convolutions to avoid the resource cost of floating-point units while maintaining visual quality.
3. **Synchronous 540p Timing**: Standardized on 960x540p to maximize throughput while staying within the DE10-Nano's pixel clock limits.
4. **Hybrid Dithering strategy**: Decouples the spatiotemporal process into a memory-less temporal domain (Pass 1) and a single-line-buffered spatial domain (Pass 2), achieving premium visual character with zero external memory bandwidth overhead.


---

## 4. Clock Domain Crossing (CDC) Design

The system operates across two primary clock domains. Proper synchronization is implemented to prevent metastability and ensuring data integrity.

### 4.1 Clock Domains
| Clock Name | Frequency | Responsibility |
|------------|-----------|----------------|
| `clk_50`   | 50 MHz    | System Clock, CSR Interface, DMA Master, FIFO Write |
| `clk_hdmi` | 37.8 MHz  | Pixel Clock, Sync Gen, Image Filters, FIFO Read |

### 4.2 Synchronization Mechanisms

The following 5 paths manage data and control flow across domains:

1.  **Video Data Path (50MHz → 37.8MHz)**:
    - **Mechanism**: Asynchronous FIFO (`DC_FIFO`).
    - **Implementation**: Uses Gray-coded pointers for safe crossing of read/write pointers across domains. The FIFO handles internal synchronization and timing closure.
2.  **V-Sync Edge Detection (37.8MHz → 50MHz)**:
    - **Mechanism**: Toggle-Synchronizer + Edge Detect.
    - **Implementation**: `vs_toggle` in the Pixel domain toggles on every V-Sync. In the 50MHz domain, it is sampled through a 3-stage shift register (`vsync_toggle_sync_50`). An **XOR** of stages [2] and [1] extracts the edge.
    - **Why XOR?**: Detects any transition (0→1 or 1→0) of the toggled signal, ensuring short pulses aren't missed by a slower clock.
3.  **CSR Status Synchronization (37.8MHz → 50MHz)**:
    - **Mechanism**: Multi-stage (Dual-flop) Synchronizer.
    - **Implementation**: The `vs_toggle` signal is sampled into a 2-stage register (`vs_toggle_sync`) in `hdmi_sync_gen.v` so the Nios II can safely read the V-Sync status via the Avalon-MM interface.
4.  **Frame Pointer Snap (37.8MHz → 50MHz)**:
    - **Mechanism**: Pulse Synchronizer (Edge Detect).
    - **Implementation**: The internal `vs_wire` (Pixel domain) is sampled into a 3-stage register (`vs_sync_sh`) in the 50MHz domain. On the rising edge of the synchronized signal, `reg_frame_ptr` is latched into `shadow_ptr` to ensure a stable address for the next frame.
5.  **Steady-State Control Signals (50MHz → 37.8MHz)**:
    - **Mechanism**: Direct sampling.
    - **Implementation**: Signals like `reg_mode` and `reg_global_ctrl[0]` (Gamma) are "quasi-static" (changed only via software and stable for millions of clock cycles). They are sampled directly in the `clk_hdmi` domain.

---

## 5. Final Verification
1. **RTL Simulation (Cocotb)**: Cycle-accurate verification using real `.raw` image data.
2. **Target Hardware**: Verified stable 60fps output on HDMI monitors.
