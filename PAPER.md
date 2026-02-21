# A Memory-Efficient Hybrid Spatiotemporal Dithering Architecture for Low-Grayscale Compensation in MicroLED Displays

## Abstract
The physical emission limitations of MicroLED and OLED displays in ultra-low grayscale regions frequently result in severe black crush and color banding artifacts. While 3D spatiotemporal error diffusion can mitigate these issues, it necessitates a massive external frame buffer, creating a prohibitive memory bandwidth bottleneck. This paper proposes a memory-efficient, bit-split hybrid spatiotemporal dithering architecture that achieves high-fidelity low-grayscale compensation without requiring any external frame buffer. The proposed hardware architecture splits the pixel truncation process into two distinct stages: (1) a memory-less temporal Frame Rate Control (FRC) applied to the 2-bit LSBs, and (2) a spatial error diffusion performed on the subsequent 4-bit truncation level utilizing a single internal line buffer. Experimental results on a Cyclone V SoC FPGA demonstrate a 3.3dB improvement in PSNR for near-black regions while achieving a 100% reduction in external memory bandwidth overhead compared to conventional 3D error diffusion.

## I. INTRODUCTION
... (Sections I and II truncated for brevity in this file, same as revised_paper.md content) ...

## III. PROPOSED HYBRID ARCHITECTURE

### A. Bit-Split Strategy
Instead of applying a monolithic dithering algorithm, we partition the $K$-bit truncation into two sub-planes: the lowest 2 bits are handled in the temporal domain, and the next 4 bits are handled in the spatial domain.

### B. Two-Pass Sequential Pipeline
1. **Pass 1: Temporal Ordered Dither**: Injects 2-bit Bayer noise. Pixels > 0x04 are bypassed to maintain detail.
2. **Pass 2: Conditional Error Diffusion**: Applies Floyd-Steinberg diffusion with a user-configurable threshold.

### C. Seamless Error Propagation
Mathematical energy conservation is maintained via continuous error residue calculation:
$$P_{diffused} = P_{in} + \sum e_{neighbors}$$
$$e_{out} = P_{diffused} - P_{out}$$
This ensures that even when quantization is bypassed in bright regions, the accumulated error from dark regions propagates smoothly without inducing halo artifacts.

## IV. EXPERIMENTAL RESULTS

### A. Hardware Resource Utilization
Synthesis for 960x540p @ 60fps on Cyclone V SoC.

| Architecture | ALMs | BRAM (M10K) | External DDR Bandwidth |
| :--- | :--- | :--- | :--- |
| **Proposed Hybrid** | **~420** | **1 (FIFO)** | **0 MB/s** |
| Conventional 3D | ~1,200 | 0* | ~124.4 MB/s |

### B. Quantitative Visual Quality (PSNR)
Comparison of 8-bit original vs. 4-bit truncation and proposed dithering.

| Evaluation Region | Truncation (Baseline) | **Proposed Hybrid** | **Improvement** |
| :--- | :--- | :--- | :--- |
| Whole Image | 29.16 dB | **32.46 dB** | **+3.30 dB** |
| Near-Black (< 0x20) | 29.15 dB | **32.44 dB** | **+3.29 dB** |

![Scientific PSNR Metric Comparison](./doc/images/psnr_metrics_graph.png)

## V. CONCLUSION
The proposed architecture provides professional-grade low-grayscale compensation with zero external memory cost, making it ideal for high-resolution, high-refresh-rate display controllers.
