# Debugging Log: Video DMA & FIFO Integration
[**English**] | [**한국어**](./TRYERROR_kor.md)

This document records the issues encountered and solutions implemented during the verification of the Video DMA pipeline.

## 1. FIFO Usage Calculation Error (Critical RTL Bug)
- **Component**: `RTL/simple_dcfifo.v`
- **Symptom**: DMA Master would unpredictably stop reading from memory (stuck in `CHECK_FIFO` state) or overflow the FIFO.
- **Root Cause**: Invalid arithmetic operation mixing different coding schemes. The logic subtracted a **Gray Code** pointer directly from a **Binary** pointer to calculate usage (`wrusedw`).
    ```verilog
    // BAD CODE
    assign wrusedw = wr_ptr_bin - rd_ptr_gray_sync; 
    ```
    Since Gray codes are not weighted (e.g., 3 is `0010`, 4 is `0110`), subtraction yields meaningless results.
- **Fix**: Implemented a `gray2bin` function to convert the synchronized read pointer back to binary before subtraction.
    ```verilog
    // FIXED CODE
    wire [ADDR_WIDTH:0] rd_ptr_bin_sync = gray2bin(rd_ptr_gray_sync2);
    assign wrusedw = (used_diff[ADDR_WIDTH]) ? {ADDR_WIDTH{1'b1}} : used_diff[ADDR_WIDTH-1:0]; // Includes saturation
    ```

## 2. Implicit Net Declaration & Truncation
- **Component**: `RTL/video_pipeline.v`
- **Symptom**: Integration tests ran but data verification failed completely (received garbage or zeros).
- **Root Cause**: Missing `wire` declarations for multi-bit internal signals.
    Verilog defaults undeclared signals to **1-bit wire**.
    The 32-bit `fifo_wr_data` and 9-bit `fifo_used` signals were implicitly declared as 1-bit, causing the upper bits to be silently discarded.
- **Fix**: Added explicit wire declarations for all internal interconnects.
    ```verilog
    wire [31:0] fifo_wr_data;
    wire [8:0]  fifo_used;
    // ...
    ```

## 3. Simulation 'X' Propagation
- **Component**: `RTL/simple_dcfifo.v` & `cocotb`
- **Symptom**: Python testbench crashed with `ValueError: Cannot convert Logic('X') to bool`.
- **Root Cause**: In hardware, registers power up to unknown states ('X'). While real hardware eventually settles or uses Reset, the simulation (Cocotb) strictly enforces 4-state logic. The FIFO output `q` was 'X' until the first read, crashing the testbench comparators.
- **Fix**: Added an `initial` block to Initialize output registers to `0` for simulation purposes.
    ```verilog
    initial begin
        q = 0; // Prevent X propagation
    end
    ```

## 4. Testbench Bus Contention
- **Component**: `tests/cocotb/tb_dma_master.py` (Avalon Memory Model)
- **Symptom**: Data read from memory was corrupted or lost during burst transfers.
- **Root Cause**: The initial testbench spawned a new independent logic thread (`cocotb.start_soon`) for *every* read request. When the DMA pipeline issued multiple requests quickly, these threads tried to drive the shared `m_readdata` bus signals simultaneously (Bus Contention).
- **Fix**: Refactored the memory model to use a **Queue**.
    1.  `Monitor`: Pushes Read Requests into a `Queue`.
    2.  `Driver`: A single thread pulls requests from the `Queue` and drives the response bus sequentially.

## 5. Clock Domain Crossing & Latency
- **Component**: `tests/cocotb/tb_video_integration.py`
- **Symptom**: Pixel data mismatch at the very start of the frame (Pixel 0 was wrong).
- **Root Cause**: The Asynchronous FIFO has a inherent **1-cycle read latency**. When `rdreq` goes high, data appears on `q` one clock later. The testbench was checking `q` on the same cycle as `rdreq`.
- **Fix**: Updated the testbench pixel checker to be "latency tolerant", identifying the start of the frame sequence (`0, 1, 2...`) even if it is delayed by a cycle.

## 6. HDMI Pipeline Depth Mismatch (1-Pixel Shift)
- **Component**: `RTL/hdmi_sync_gen.v`
- **Symptom**: In simulation, pixels appeared shifted to the right by 1-2 positions (e.g., Pixel 960 appearing as the last pixel of Line 0).
- **Root Cause**: The control signals (`DE`, `HS`, `VS`) were registered through 3 pipeline stages, while the pixel data path (FIFO output + internal registration) only had a 2-cycle latency. This caused control signals to lag behind the data.
- **Fix**: Reduced the pipeline depth for `hdmi_de`, `hdmi_hs`, and `hdmi_vs` to **2 stages** in `hdmi_sync_gen.v`.

## 7. DMA Multi-Frame Wrap-around Failure
- **Component**: `RTL/video_dma_master.v`
- **Symptom**: Frame 0 verified correctly, but Frame 1 started from the wrong memory address (mid-image).
- **Root Cause**: The DMA master had hardcoded `H_RES=1280` and `V_RES=720` as default parameters. Since these were not overridden in `video_pipeline.v`, the DMA never reached its "end of frame" count (921,600 words) to trigger the address reset on the next V-Sync.
- **Fix**: Updated `video_pipeline.v` to pass the correct parameters (`960x540`, ~518,400 words).

## 8. Testbench Sampling Fidelity (Cocotb)
- **Component**: `tests/cocotb/tb_video_integration.py`
- **Symptom**: Unstable simulation results; sometimes shifted by 1 pixel, sometimes correct.
- **Root Cause**: Sampling registered signals (`hdmi_d`, `hdmi_de`) exactly on the `RisingEdge` is prone to race conditions in simulation (delta-cycle issues). 
- **Fix**: Implemented the `await ReadOnly()` trigger after `RisingEdge` to ensure sampling occurs only after all signals have stabilized for the current delta cycle.
