# Study Notes: HDMI Video Pipeline Implementation

This document provides technical details required to implement a custom HDMI video pipeline on the DE10-Nano, specifically focusing on 1280x720 (720p) resolution.

## 1. 720p (1280x720 @ 60Hz) Video Timing
To display a stable image, the Sync Generator must adhere to the CEA-861 standard for 720p resolution.

| Parameter | Horizontal (Pixels) | Vertical (Lines) |
| :--- | :--- | :--- |
| **Visible Area** | 1280 | 720 |
| **Front Porch** | 110 | 5 |
| **Sync Pulse** | 40 | 5 |
| **Back Porch** | 220 | 20 |
| **Total Area** | 1650 | 750 |
| **Pixel Clock** | **74.25 MHz** | - |

- **Refresh Rate**: $1650 \times 750 \times 60 \text{ Hz} \approx 74.25 \text{ MHz}$.
- **Data Enable (DE)**: High only during the visible area ($0 \leq X < 1280$ and $0 \leq Y < 720$).

## 2. ADV7513 HDMI Transmitter Configuration (I2C)
The ADV7513 must be initialized via I2C before it can transmit video signals.

- **I2C Slave Address**: `0x72` (or `0x7A` depending on the board).
- **Core Registers**:
    - `0x41[6]`: **Power Down Control**. Bit 6 logic `0` means "Power Up". Default is often 1 (standby).
    - `0x16[5:4]`: **Color Depth**. `00` selects 8-bit per channel (Total 24-bit RGB).
    - `0x16[3:0]`: **Video Format**. `0000` selects standard RGB 4:4:4 input.
    - `0xAF[1]`: **HDCP/HDMI Mode**. Bit 1 logic `1` enables HDMI mode (required for audio and info-packets).
    - `0x98, 0x9A...`: **Magic/Fixed Registers**. The internal analog circuitry requires specific values (e.g., `0x98=0x03`) to function correctly as per the programming guide.

### Nios II Implementation Example
In our C code, we will use an I2C write function to configure these during startup.

```c
void hdmi_init() {
    printf("Initializing ADV7513 HDMI Transmitter...\n");
    
    // 1. Power up the device (Clear bit 6 of Reg 0x41)
    hdmi_i2c_write(0x41, 0x10); // Bit 6=0, other bits depend on chip revision

    // 2. Set Input Format (RGB 4:4:4, 8-bit)
    hdmi_i2c_write(0x16, 0x00); 

    // 3. Select HDMI Mode (Reg 0xAF bit 1 = 1)
    // We read-modify-write or just set typical values
    hdmi_i2c_write(0xAF, 0x06); // Standard HDMI mode

    // 4. Fixed 'Magic' sequence (Required for stable operation)
    hdmi_i2c_write(0x98, 0x03);
    hdmi_i2c_write(0x9A, 0xE0);
    hdmi_i2c_write(0x9C, 0x30);
    hdmi_i2c_write(0x9D, 0x61); 
    
    printf("HDMI Controller Configured.\n");
}
```

## 3. Custom Sync Generator Logic (Verilog)
The Sync Generator uses two nested counters to manage the horizontal and vertical position.

### Counter Logic
```verilog
always @(posedge pix_clk or posedge reset) begin
    if (reset) begin
        h_cnt <= 0;
        v_cnt <= 0;
    end else begin
        if (h_cnt == H_TOTAL - 1) begin
            h_cnt <= 0;
            if (v_cnt == V_TOTAL - 1)
                v_cnt <= 0;
            else
                v_cnt <= v_cnt + 1;
        end else begin
            h_cnt <= h_cnt + 1;
        end
    end
end
```

### Signal Generation
- **HSync**: Active (typically low) when $h\_cnt$ is within the Sync Pulse range.
- **VSync**: Active (typically low) when $v\_cnt$ is within the Sync Pulse range.
- **Data Enable (DE)**: High when $h\_cnt < 1280$ AND $v\_cnt < 720$.

## 4. Interface & Back-pressure Mechanism (Handshake)
To ensure that pixel data is only fetched when it needs to be displayed, we implement a back-pressure mechanism using the **Avalon-ST Handshake**.

### Handshake Signal Roles
- **`asi_data`**: 24-bit RGB pixel data.
- **`asi_valid`**: High when the FIFO has at least one pixel of data available.
- **`asi_ready`**: Controlled by the **Sync Generator**. High only during the active display period.

### Back-pressure Logic
The Sync Generator acts as the "Consumer" and controls the flow of data from the "Producer" (Video DMA/FIFO) based on the current scanline position.

| Pipeline State | Data Enable (DE) | Interface `ready` | Action |
| :--- | :---: | :---: | :--- |
| **Visible Area** | 1 | 1 | Fetch pixel data from FIFO every clock cycle. |
| **Porch / Sync** | 0 | 0 | Pause data fetch; FIFO and DMA wait in current state. |

### Propagation of Back-pressure
1. **Sync Generator** deasserts `ready` during blanking intervals.
2. **DCFIFO** output stops providing data, causing its internal level to rise.
3. When **DCFIFO** becomes "Full" (or reaching a threshold), it deasserts `ready` to the **Video DMA**.
4. **Video DMA** pauses its Avalon-MM read transactions to DDR3.

## 5. Role of HDMI Transmitter (ADV7513)
The ADV7513 is a high-performance HDMI transmitter that bridges the FPGA logic and the monitor.

### How it works with FPGA signals
- **Clocked Sampling**: It samples the 24-bit RGB data and Sync signals (HSync, VSync, DE) at every edge of the `HDMI_TX_CLK`.
- **TMDS Conversion**: It encodes these parallel signals into high-speed **TMDS (Transition Minimized Differential Signaling)** pairs that travel through the HDMI cable.
- **Data Enable (DE) is King**: The chip relies heavily on the `DE` signal. When `DE` is high, it treats the inputs as pixel data; when `DE` is low, it can embed audio data or secondary packets into the stream.

### Why I2C Initialization is Essential
Even though it "follows" our sync signals, the chip won't output anything until:
1. **Power-up**: We send an I2C command to wake it from standby.
2. **Signal Mapping**: We tell it how our 24 bits are mapped (e.g., RGB 4:4:4 vs. YCbCr).
3. **HDMI Mode**: We explicitly enable HDMI mode (as opposed to DVI).

In summary, once the Nios II initializes the chip via I2C, it becomes a "transparent pipe" that project our FPGA's timing and pixels directly onto the screen.

## 6. Advanced Topic: HDMI Without ADV7513?
Yes, it is possible to implement HDMI without a dedicated chip, but it requires significantly more FPGA logic and specific hardware capabilities.

### Requirements for Direct HDMI Output
- **TMDS Encoding (RTL)**: Digital RGB data must be converted to 10-bit TMDS characters using an 8b/10b encoding algorithm in Verilog.
- **Serialization (10:1)**: Since HDMI is a serial protocol, the 10-bit parallel data must be serialized at 10x the pixel clock. For 720p (74.25 MHz), this means a bit rate of **742.5 Mbps** per lane.
- **Differential I/O**: The FPGA must support differential output standards (like TMDS or LVDS) on its physical pins to drive the HDMI connector directly.
- **Level Shifting**: HDMI uses 3.3V signals. If the FPGA IO bank is at a different voltage, level shifters are required.

### Complexity Comparison
| Feature | With ADV7513 (Our Project) | Without Transmitter (Direct) |
| :--- | :--- | :--- |
| **FPGA Logic** | Simple Parallel Interface | Complex TMDS + SERDES |
| **Clocking** | Pixel Clock (74.25 MHz) | 10x Clock (742.5 MHz) |
| **Difficulty** | ★☆☆☆☆ | ★★★★☆ |

Using the ADV7513 allows us to focus on the **video processing logic** (DMA, Filter, Pattern Gen) rather than the low-level physical layer of the HDMI protocol.

## 7. What is 8b/10b Encoding?
8b/10b encoding is a line code that maps 8-bit symbols to 10-bit symbols to achieve specific physical layer goals in high-speed serial communication.

### Why use 10 bits for 8-bit data?
1. **DC Balancing (Preventing DC Bias)**:
   - If a signal stays at '1' or '0' for too long, a charge builds up in the transmission line or AC-coupling capacitors.
   - 8b/10b ensures that the number of '1's and '0's over time is roughly equal, maintaining a constant DC level of zero.
2. **Clock Recovery (Constant Transitions)**:
   - Serial links like HDMI don't send a separate clock line per data lane. The receiver must "extract" the clock from the data.
   - 8b/10b guarantees that there are enough transitions (0 to 1 or 1 to 0) so the receiver's PLL can stay locked onto the bitstream.

### TMDS (The HDMI Version)
HDMI uses a specialized version called **Transition Minimized Differential Signaling (TMDS)** encoding.
- **Stage 1**: XOR or XNOR operations to minimize the number of transitions (to reduce EMI).
- **Stage 2**: DC-balancing by selectively inverting the data to maintain the average voltage level.

This process transforms our simple 8-bit RGB color values into robust 10-bit packets that can travel across several meters of HDMI cable without losing a single bit.

## 8. Professional Context: LVDS vs HDMI/TMDS
In industrial and professional settings, **LVDS (Low Voltage Differential Signaling)** is often used for internal display connections (e.g., Laptop panels, TV T-CON boards).

### LVDS (Low Voltage Differential Signaling)
- **Clocking**: Whereas HDMI/TMDS uses 10-bit encoding (10:1), standard LVDS often uses **7:1 serialization** (OpenLDI standard).
- **Data Density**: 7 bits of data are sent per clock cycle per lane.
- **UHD Challenges**: For UHD (3840x2160), the data rate is astronomical (~12Gbps). A single LVDS lane cannot handle this.
- **Solution for UHD**: Companies use **Multi-lane LVDS** (Dual, Quad, or even 8-lane) or switch to newer standards like **V-by-One HS**, which can reach higher speeds per lane (up to 4Gbps) and uses 8b/10b encoding (unlike traditional LVDS).

### Interface Comparison
| Interface | Encoding | Serialization | Primary Use Case |
| :--- | :--- | :--- | :--- |
| **HDMI/TMDS** | 8b/10b (TMDS) | 10:1 | External monitors, TV |
| **Standard LVDS** | None (Raw) | 7:1 | Internal laptop/TV panels |
### V-by-One HS (The Standard for UHD/4K)
Developed by THine Electronics, **V-by-One HS** is the de facto standard for connecting the main board to the T-CON (Timing Controller) in modern 4K/8K TVs.

- **Encoding**: Uses **8b/10b encoding**, which is a huge upgrade from the "Raw" 7:1 format of LVDS. This ensures DC balance and allows for simpler AC coupling.
- **Clock Recovery (CDR)**: Unlike LVDS which requires a separate clock pair, V-by-One HS embeds the clock into the data stream (Clock Data Recovery), significantly reducing EMI and cable count.
- **Speed**: While LVDS hits a wall around 1Gbps, V-by-One HS can push up to **4Gbps per lane**.
- **Efficiency**: For a 4K 60Hz 10-bit panel, you would need about 24 pairs of LVDS, but only **8 lanes** of V-by-One HS.

## 9. Measuring Success: The Eye Diagram
An **Eye Diagram** (or Eye Pattern) is a visual tool used to evaluate the signal integrity of high-speed digital links (like HDMI, LVDS, V-by-One).

### What is it?
- It is generated by an oscilloscope by overlaying multiple periods of the data signal on top of each other.
- If the signal is stable and has low noise/jitter, the resulting image looks like an open "eye".

### How to Interpret It
- **Eye Opening (Height)**: Indicates the noise margin. A larger height means it's easier to distinguish '0' from '1'.
- **Eye Width**: Indicates the jitter and timing margin. A wider eye means the timing is stable.
- **Eye Closing**: If the eye is closed or blurry, it means the signal has too much interference (cross-talk, reflection, or attenuation), and the receiver will likely fail to recover the data.

### Connection to HDMI
HDMI Compliance testing strictly specifies an "Eye Mask". The resulting eye diagram must not penetrate this central region to be considered a viable, standard-compliant signal.

| Factor | Effect on Eye Diagram |
| :--- | :--- |
| **Good SI** | Large, clear open area in the center |
| **Jitter** | Horizontal blur (eye gets narrower) |
| **Noise** | Vertical blur (eye gets shorter) |
| **Losses** | Overall shrinkage and rounding of edges |

Understanding the Eye Diagram is the ultimate way to prove that your high-speed Verilog logic and physical PCB layout are working perfectly together!

## 10. The Hardware Engine: SERDES
**SERDES** stands for **Serializer / Deserializer**. It is the fundamental hardware block used to convert parallel data into serial data (and vice-versa) for high-speed transmission.

### How it relates to our project
1. **Serialization (TX Side)**: Inside the FPGA (or HDMI chip), 10-bit or 8-bit parallel data is fed into a shift register that runs at a very high clock speed, spitting out bits one-by-one onto the differential pairs.
2. **Deserialization (RX Side)**: The monitor's receiver takes that "stream" of bits and reconstructs the parallel 10-bit/8-bit symbols using Clock Data Recovery (CDR).

### Key Features
- **PISO / SIPO**: Parallel-In Serial-Out (PISO) for transmission, and Serial-In Parallel-Out (SIPO) for reception.
- **Integration**: In many high-end FPGAs, SERDES is a dedicated "Hard IP" block because regular fabric logic cannot toggle fast enough (e.g., >1 Gbps).
- **The Core of Everything**: HDMI (10:1), LVDS (7:1), and V-by-One all use SERDES as their base "physical engine."

By combining **8b/10b encoding** (the logic), **SERDES** (the hardware engine), and **Eye Diagrams** (the validation), we complete the trifecta of high-speed digital design!

## 11. IP vs. Custom Implementation & Licensing
In the industry, choosing between using an IP (Intellectual Property) or writing custom RTL is a critical engineering decision.

### Hard IP vs. Soft IP
- **Hard IP (SERDES/Transceivers)**: You **must** use the FPGA's Hard IP for the high-speed physical layer. Regular Verilog logic can't toggle at several Gbps. These are usually provided for free by the FPGA vendor (e.g., Intel's *Native PHY IP*).
- **Soft IP (Protocol Controllers)**: For the logic part (HDMI controller, TMDS encoder), you can choose to buy a commercial IP or write your own. Writing your own is a great way to save costs and gain deep technical knowledge, which is exactly what we are doing!

### Licensing and Royalties
- **Standard Licenses**: Using the HDMI name and logo requires becoming an HDMI Adopter and paying annual fees + royalties (e.g., $0.04 - $0.15 per device).
- **IP Costs**: Commercial HDMI IPs from companies like Synopsys or Cadence can cost tens of thousands of dollars.
- **Why we write custom RTL**: By writing our own Sync Generator and using basic AXI/Avalon interfaces, we avoid expensive IP licensing fees and learn the "guts" of the system.

Even if you use a Hard IP, knowing how the underlying logic works is what separates a senior engineer from a junior one!

## 12. Comparison: Legacy VGA vs. Modern HDMI
If you have experience with VGA output on boards like DE1, you'll find some familiar concepts, but also major "Level-Up" challenges.

| Feature | VGA (Legacy/DE1) | HDMI (ADV7513/DE10-Nano) |
| :--- | :--- | :--- |
| **Physical Layer** | Analog (Resistor Ladder/DAC) | Digital Serial (TMDS via Transmitter Chip) |
| **Sync Logic** | HSync/VSync Generator | Same, but also requires **Data Enable (DE)** |
| **Setup** | Hardware Only (Wiring) | **H/W + S/W** (Requires I2C Config) |
| **Source** | Often ROM/On-Chip RAM | **External DDR3 via DMA (AXI)** |
| **Resolution** | Usually 640x480 (25MHz) | **1280x720 (74.25MHz)** or higher |

### Why HDMI is more advanced:
1. **Control Logic**: You have to manage the ADV7513 through a Nios II driver. If I2C fails, you get a black screen even if your RTL is perfect.
2. **System Context**: Fetching pixels from DDR3 using a Burst DMA is significantly more complex than reading from a small internal buffer.
3. **Signal Integrity**: High-speed digital signals (74.25MHz+) are much more sensitive to timing delays and skew than legacy VGA.

## 13. Memory Choice: SRAM vs. DDR3
In legacy projects like DE1 VGA, **SRAM** was often used because of its simplicity, but modern video processing requires the high capacity of **DDR3**.

| Feature | Legacy SRAM (DE1) | Modern DDR3 (DE10-Nano) |
| :--- | :--- | :--- |
| **Complexity** | Simple (Direct Address/Data) | Very Complex (DDR Controller/AXI Protocol) |
| **Capacity** | Very Small (e.g., 512KB) | Very Large (1GB) |
| **Latency** | Extremely Low (Fixed) | High/Variable (Requires Bursts & FIFOs) |
| **Connection** | Direct FPGA Pin to Chip | Via HPS Bridge & Interconnect Logic |

- **SRAM Implementation**: You probably used a simple state machine to drive the address pins and read the data directly into your Sync Generator.
- **DDR3 Implementation**: Since we cannot drive DDR3 directly from a simple FSM, we use a **Burst DMA** to fetch chunks of data and a **FIFO** to smooth out the variable latency, ensuring the Sync Generator always has a pixel ready when needed.

## 14. Smooth Video: Double Buffering
To prevent "Screen Tearing" (where parts of two different frames are visible at once), **Double Buffering** is essential in DDR3-based video systems.

### The Problem: Screen Tearing
- If the ARM or Nios II updates the DDR3 memory while the Video DMA is reading it, the monitor might display the top half of the *new* frame and the bottom half of the *old* frame.

### The Solution: Front & Back Buffers
- **Front Buffer**: The memory region currently being read by the Video DMA and displayed on the monitor.
- **Back Buffer**: A separate memory region where the next frame is being prepared/drawn.
- **Buffer Swapping (V-Sync Switching)**: Once the Back Buffer is ready, we wait for the **Vertical Blanking Interval (V-Sync)** to update the DMA's start address. This ensures the switch happens only when no active pixels are being drawn.

### Implementation in DDR3
- **Address Management**: Since we have plenty of space in DDR3 (1GB!), we can easily allocate two 32MB regions.
    - Buffer A: `0x20000000`
    - Buffer B: `0x22000000`
- **Ping-Pong Logic**: The software writes to Buffer B while the hardware reads from Buffer A. After the frame is done, they swap roles.

This technique is what makes video look professional and fluid rather than glitchy!

## 15. Loading Video Data: SD Card ➡️ DDR3
The most practical way to put a large "movie" or image sequence into DDR3 is using the **ARM Cortex-A9 (HPS)** running Linux.

### The Pipeline
1. **Store**: Copy video files (e.g., raw pixel data or BMP sequence) to the SD card via SCP or SFTP.
2. **Read**: Use a C or Python application on Linux to read the file from the filesystem.
3. **Map**: Use `mmap()` on `/dev/mem` to map the physical DDR3 memory address (e.g., `0x20000000`) into the application's virtual address space.
4. **Write**: Copy the pixel data from the file buffer into the mapped DDR3 region.

### Real-time Video Playback Flow
For a continuous movie, the ARM processor acts as the "Feeder":
- **Phase A**: ARM reads Frame 1 from SD ➡️ writes to **Back Buffer (DDR3)**.
- **Phase B**: ARM sends a sync signal to Nios II (or a shared register) ➡️ Hardware swaps to display Back Buffer.
- **Phase C**: ARM reads Frame 2 from SD ➡️ writes to the now-idle **Front Buffer**.

By repeating this at 30 or 60 times per second, you get a full-speed movie playing directly from your SD card onto your HDMI monitor!

## 16. Real-time Decoding: MP4 and CPU Limits
Handling compressed formats like **MP4 (H.264)** is much more CPU-intensive than just copying raw pixel data.

### Can ARM (Cortex-A9) handle it?
- **Software Decoding**: Using libraries like **FFmpeg (libav codec)**, the dual-core 800MHz A9 can handle 480p or basic 720p at 24/30fps. However, reaching 60fps for 720p/1080p via pure software is very difficult.
- **NEON Acceleration**: To make it work, the code must use the **NEON SIMD engine** inside the Cortex-A9 cores. This allows the CPU to process multiple data points in parallel, which is critical for video decoding.
- **The Bottleneck**: The HPS on Cyclone V doesn't have a dedicated hard-wired H.264 decoder (VPU). Therefore, the CPU must do all the heavy lifting (Calculating DCT, Entropy coding, etc.).

### Practical Strategies
1. **Pre-decode (Initial Phase)**: Convert MP4 to RAW RGB data on a PC first. Then, ARM just copies it. This lets us test our FPGA HDMI pipeline without worrying about CPU limits.
2. **Optimized Software**: Use `ffplay` or customized C code using `libavcodec` with NEON enabled.
3. **Hardware Assist (Advanced)**: Implement partial decoding acceleration (like a color space converter) in the FPGA fabric to offload the ARM.

Understanding these CPU limits is the first step toward building a balanced "System-on-Chip" where software and hardware share the load efficiently!

## 17. The Format After Decompression: Raw Video
When you "unpack" an MP4 file on a PC, you get **Raw Video**. This means every single pixel is laid out in memory without any compression.

### Common Raw Formats
1. **RGB888 (24-bit)**:
   - Each pixel = 1 byte Red + 1 byte Green + 1 byte Blue.
   - **Size per frame (720p)**: $1280 \times 720 \times 3 \text{ bytes} \approx 2.76 \text{ MB}$.
   - **Pros**: Directly matches the FPGA's 24-bit RGB bus. No extra conversion needed.
2. **YUV422 / YUV420**:
   - Uses human perception (brightness vs. color) to reduce size.
   - **Pros**: Smaller file size than raw RGB.
   - **Cons**: Requires a "Color Space Converter" (CSC) in the FPGA to turn it back into RGB888 for the HDMI chip.

### How to generate these on PC?
You can use **FFmpeg**, the Swiss Army knife of video, to convert any movie into a raw format our FPGA can easily read:

```bash
# Convert MP4 to Raw RGB888 (2.6MB per frame)
ffmpeg -i movie.mp4 -f rawvideo -pix_fmt rgb24 output_720p.raw
```

The resulting `.raw` file is just a massive stream of bytes. Our ARM processor just needs to read 2.76 MB chunks and copy them to DDR3 to play the video!

## 18. Hardware Optimization: YUV422 to RGB888 CSC
Incorporating a **Color Space Converter (CSC)** into the pipeline allows you to store video in YUV422 format (16 bits per pixel) instead of RGB888 (24 bits per pixel), saving **33% of DDR3 bandwidth**.

### YUV422 Data Structure
- **Pixel 1**: $Y_0$ and share $U_0, V_0$.
- **Pixel 2**: $Y_1$ and share the same $U_0, V_0$.
- Data is typically ordered as: `Y0, U0, Y1, V0, Y2, U1, Y3, V1...`

### The Conversion Formula (Fixed Point)
Since FPGAs aren't great at floating-point math, we use integer approximations (multiplication and bit-shifting):
- $R = [1.164(Y - 16) + 1.596(V - 128)]$
- $G = [1.164(Y - 16) - 0.813(V - 128) - 0.391(U - 128)]$
- $B = [1.164(Y - 16) + 2.018(U - 128)]$

### Pipeline Integration
1. **MM2ST Decoder**: Reads 16-bit YUV422 chunks from DDR3.
2. **CSC Module**: Contains multipliers and adders to calculate RGB values in real-time.
3. **FIFO**: Stores the resulting 24-bit RGB pixels.
4. **Sync Generator**: Pulls RGB pixels from the FIFO as needed.

By adding this one module, we can handle higher resolutions or save bandwidth for other ARM/HPS tasks!

## 19. Visual Quality: Gamma Correction ($\gamma$)
**Gamma Correction** is the process of compensating for the non-linear relationship between the pixel value and the actual brightness perceived by the human eye.

### Why do we need it?
- **Human Perception**: Our eyes are more sensitive to variations in dark tones than in bright ones.
- **Display Response**: Old CRTs and modern LCDs don't have a linear response ($Intensity \propto Voltage^\gamma$).
- Without Gamma Correction ($\gamma = 2.2$), images tend to look "washed out" or have incorrect contrast in the mid-tones.

### FPGA Implementation: Look-Up Table (LUT)
Calculating $V_{out} = V_{in}^{1/\gamma}$ in real-time using mathematical formulas is extremely hardware-heavy. Instead, we use a **LUT**.

1. **Pre-calculation**: On a PC, you calculate the correct 8-bit output for every possible 8-bit input (0-255) based on the gamma curve.
2. **Memory Map**: Store these 256 values in a small Dual-Port RAM or ROM inside the FPGA.
3. **The Pipeline**:
   - `Input Pixel (8-bit)` ➡️ `Address of LUT`
   - `Data at Address` ➡️ `Gamma-Corrected Pixel (8-bit)`
4. **Integration**: This can be placed right before the HDMI transmitter to fine-tune the final output quality.

### Integration: Encoding vs. Correction
You've made a very sharp point: Most monitors already have a "Gamma Response." Because of this, we usually distinguish between two terms:

1. **Gamma Encoding (Source Side)**: This is what the FPGA or the PC does. Since monitors have a non-linear response, we **encode** the data (usually with $\gamma=1/2.2$) so that when the monitor applies its natural "Gamma Correction" (usually $\gamma=2.2$), the final result is a **linear** 1-to-1 brightness.
2. **Pre-encoded Content**: Most MP4, BMP, and JPEG files are **already gamma-encoded** by the camera or the software that created them. In this case, the FPGA should **not** apply any further gamma correction—it should just pass the bits through!
3. **When to use the LUT?**: You only need a Gamma LUT in the FPGA if:
   - Your FPGA is generating **Linear Math** (e.g., a simple gradient counter $0 \to 255$). Without encoding, the gradient will look "bunched up" in the dark areas on the monitor.
   - You are doing **Alpha Blending** or **Linear Light Video Processing** inside the FPGA fabric.

### The Shape of the Curve: Convex Upwards ($\cap$)
Your intuition is 100% correct! To match the monitor's response, the encoding curve must be **convex upwards** (위로 볼록).

- **Monitor Response (Physical)**: $I = V^{2.2}$. This curve is **concave upwards** ($\cup$). It stays dark for a long time and then shoots up suddenly. If we send linear data, the middle grey values will look way too dark.
- **FPGA Encoding (Mathematical)**: $V = I^{1/2.2} \approx I^{0.45}$. This curve is **convex upwards** ($\cap$). It shoots up quickly in the dark areas and then flattens out.

By applying this "Convex" shape in our LUT, we effectively "pre-brighten" the dark areas. When the monitor's "Concave" response pulls them back down, they land exactly where they should be for our eyes.

| Curve Type | Shape | Math | Role |
| :--- | :---: | :---: | :--- |
| **Encoding** | **Convex ($\cap$)** | $x^{0.45}$ | Pre-brighten darks (Source side) |
| **Response** | **Concave ($\cup$)** | $x^{2.2}$ | Physical display characteristic |

This is why a simple linear counter $0 \to 255$ results in a gradient that looks like it has too much "black" area on a raw monitor without this correction! 📊✨

## 20. Case Study: How does the GHRD Linux UI work?
In the Golden Hardware Reference Design (GHRD), you see a Linux desktop on the HDMI monitor. This data path is a perfect example of what we've learned.

### The Mechanism: Linux Framebuffer
1. **Memory Allocation**: During boot, the Linux kernel reserves a specific region of DDR3 (e.g., 32MB) to be used as `fb0` (Framebuffer 0).
2. **The Driver**: A dedicated Linux driver (`altvipfb` or similar) communicates with the FPGA.
3. **The IP in Qsys**: In the FPGA fabric, there is an IP called the **Intel VIP Frame Reader** (or a custom Frame Reader).
4. **The Link**:
   - **ARM Side**: The X-Server or GUI draws pixels into the Reserved DDR3 region.
   - **FPGA Side**: The Frame Reader IP is configured (via AXI-Lite) with the start address of that DDR3 region.
   - **Streaming**: The Frame Reader IP acts as a DMA Master, fetching pixels from DDR3 and converting them into an **Avalon-ST video stream**.
5. **Output**: This stream goes through a **Clocked Video Output (CVO)** IP, which generates the HSync/VSync, and finally to the ADV7513.

### Why does it feel slow?
Sometimes the Linux UI feels a bit "laggy" on FPGA boards. This is because the ARM CPU has to do all the drawing (GUI rendering) in software and then the FPGA has to compete for DDR3 bandwidth to read those pixels.

### The Sync: Handling the Swap
How does the Linux driver make sure the Frame Reader doesn't read a half-drawn frame?

1. **AXI-Lite Control**: The Frame Reader IP has a set of control registers accessible by the ARM CPU. One of these registers holds the **Start Address** of the current frame in DDR3.
2. **Double/Triple Buffering**: Linux usually maintains at least two buffers.
3. **V-Sync Interrupt**: This is the secret sauce.
   - When the Frame Reader finishes reading the last pixel of a frame (during the Vertical Blanking Interval), it sends an **IRQ (Interrupt Request)** to the ARM CPU.
   - The Linux driver receives this interrupt and knows, "Okay, the monitor just finished showing the old frame. It's safe to switch to the new one now!"
4. **The Handshake**:
   - ARM writes the *new* buffer's address to the Frame Reader's register.
   - ARM tells the Frame Reader to "Update on next V-Sync."
   - The Frame Reader waits until the current frame output is completely finished before actually switching the internal DMA address to the new location.

Understanding this flow is exactly what we are doing manually now—but instead of a complex Linux driver, we are using the **Nios II and our Custom Sync Generator** to gain full control!

## 21. Historical Context: 8086 Text Mode
In the 8086/DOS era, displaying text was much simpler for the CPU because the hardware had a dedicated **Text Mode**.

### Character Memory (0xB8000)
- Instead of managing millions of pixels, the CPU only had to manage a grid (typically **80x25** characters).
- The video memory started at physical address **`0xB8000`**.
- Each character occupied **2 bytes**:
    - **Byte 1**: ASCII code (e.g., 'A' = `0x41`).
    - **Byte 2**: Attribute (Color/Blink).

### How it worked without a Framebuffer
1. **CPU**: Writes `0x41` (A) and `0x07` (White on Black) to `0xB8000`.
2. **Video Controller**:
   - Reads the ASCII code `0x41` from memory.
   - Looks up the pixel pattern for 'A' in a **Font ROM** (Character Generator).
   - The Font ROM output the 16x8 or 8x8 pixel grid for that character.
3. **Output**: The hardware converted that ROM pattern into a VGA signal in real-time.

### Comparison to Modern HDMI Project
| Feature | 8086 Text Mode | Our Modern HDMI Pipeline |
| :--- | :--- | :--- |
| **Logic** | Character-based (Grid) | Pixel-based (Everything is a dot) |
| **CPU Burden** | Very Low (2 bytes per char) | High (3 bytes per pixel) |
| **Hardware** | Fixed Font ROM | Flexible Software/RTL Rendering |
| **Flexibility** | Fixed font and size | Unlimited (Anti-aliasing, any font) |

In modern systems like our DE10-Nano, **"Text Mode" no longer exists in hardware.** When you see text (like in our Linux UI), the ARM/Nios II has to manually "draw" each letter pixel-by-pixel into the DDR3 framebuffer using a font bitmap!
