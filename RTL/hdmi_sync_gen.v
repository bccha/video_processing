`timescale 1ns/1ps

// 960x540 (qHD @ 60Hz) HDMI Sync Generator
// Pixel Clock: ~37.8 MHz

module hdmi_sync_gen (
    input  wire        clk,       // CSR Clock (50 MHz)
    input  wire        clk_pixel, // HDMI Pixel Clock (~37.8 MHz)
    input  wire        reset_n,
    
    // HDMI Signals (Pixel Domain)
    output reg  [23:0] hdmi_d,
    output reg         hdmi_de,
    output reg         hdmi_hs,
    output reg         hdmi_vs,

    // Avalon-MM Slave Interface (CSR Domain)
    input  wire [2:0]  avs_address,
    input  wire        avs_read,
    input  wire        avs_write,
    input  wire [31:0] avs_writedata,
    output wire [31:0] avs_readdata,
    output reg         avs_readdatavalid,
    
    // Status (CSR Domain)
    output wire [31:0] reg_mode_out,
    output wire        dma_enable_out,
    output wire [31:0] shadow_ptr_out,
    
    // Stream Interface (Pixel Domain)
    input  wire [23:0] stream_data_in,
    output wire        stream_rd_en,
    
    // Status from DMA (CSR Domain)
    input  wire        dma_busy,
    input  wire        dma_done_in,
    
    // Control to DMA (CSR Domain)
    output wire        dma_start_out,
    output wire        dma_cont_en_out,
    output reg         vs_toggle // Toggle from Pixel Domain
);

    // Control Registers
    reg [31:0] reg_mode;        // Addr 0: Mode selection
    reg [31:0] reg_global_ctrl; // Addr 1: [31]Busy(R), [30]Done(RW1C), [2]Start(W), [1]Cont(RW), [0]Gamma(RW)
    reg [31:0] reg_lut_addr;    // Addr 2: LUT Address (0-255)
    reg [31:0] reg_lut_data;    // Addr 3: LUT Data (8-bit)
    reg [31:0] reg_bitmap_addr; // Addr 4: Bitmap Update Addr (0-15)
    reg [31:0] reg_bitmap_data; // Addr 5: Bitmap Update Data (16-bit)
    reg [31:0] reg_frame_ptr;   // Addr 6: Frame Pointer (DDR3 Address)
    reg [31:0] shadow_ptr;      // Internal Shadow Pointer
    
    reg        dma_start_pulse;
    reg        dma_done_sticky;

    assign reg_mode_out = reg_mode;
    assign dma_enable_out = reg_global_ctrl[1]; // Continuous Mode
    assign dma_cont_en_out = reg_global_ctrl[1];
    assign dma_start_out = dma_start_pulse;
    assign shadow_ptr_out = shadow_ptr;
    reg [11:0] h_cnt;
    reg [11:0] v_cnt;
    reg        visible_d1;
    reg        hs_d1;
    reg        vs_d1;

    initial begin
        h_cnt = 0;
        v_cnt = 0;
        visible_d1 = 0;
        hs_d1 = 0;
        vs_d1 = 0;
        hdmi_d = 0;
        hdmi_de = 0;
        hdmi_hs = 1;
        hdmi_vs = 1;
        vs_toggle = 0;
    end

    // Character Bitmap Memory (16x16)
    // Each entry is one row (16 bits)
    reg [15:0] char_bitmap [0:15];

    // LUT Memory (256x8)
    reg [7:0] lut_mem [0:255];

    // Read Logic: Explicit Case for Address Decoding
    reg [31:0] read_data_mux;
    reg [31:0] avs_readdata_reg;
    assign avs_readdata = avs_readdata_reg;

    always @(*) begin
        case (avs_address)
            3'd0:    read_data_mux = reg_mode;
            3'd1:    read_data_mux = {dma_busy, dma_done_sticky, 28'd0, reg_global_ctrl[1], reg_global_ctrl[0]}; 
            3'd2:    read_data_mux = reg_lut_addr;
            3'd3:    read_data_mux = reg_lut_data;
            3'd4:    read_data_mux = reg_bitmap_addr;
            3'd5:    read_data_mux = reg_bitmap_data;
            3'd6:    read_data_mux = reg_frame_ptr;
            default: read_data_mux = 32'd0;
        endcase
    end

    // Avalon-MM WaitRequest Logic
    // We can handle writes immediately (1 cycle), but for robustness with CDC,
    // let's add a simple wait state machine or just drive waitrequest low after 1 cycle.
    // Actually, for this simple slave, we can just be always ready (waitrequest=0)
    // BUT, if the Nios is too fast, we might need it. 
    // Let's implement a simple 1-cycle wait for READs to ensure data is stable.
    
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            reg_mode <= 32'd0;
            reg_global_ctrl <= 32'd0; 
            reg_lut_addr <= 32'd0;
            reg_lut_data <= 32'd0;
            reg_frame_ptr <= 32'h30000000;
            avs_readdatavalid <= 1'b0;
            dma_start_pulse <= 1'b0;
            dma_done_sticky <= 1'b0;
            dma_done_sticky <= 1'b0;
            // Initialize bitmap to 0... [Omitted]
             char_bitmap[0] <= 16'd0; char_bitmap[1] <= 16'd0; char_bitmap[2] <= 16'd0; char_bitmap[3] <= 16'd0;
            char_bitmap[4] <= 16'd0; char_bitmap[5] <= 16'd0; char_bitmap[6] <= 16'd0; char_bitmap[7] <= 16'd0;
            char_bitmap[8] <= 16'd0; char_bitmap[9] <= 16'd0; char_bitmap[10] <= 16'd0; char_bitmap[11] <= 16'd0;
            char_bitmap[12] <= 16'd0; char_bitmap[13] <= 16'd0; char_bitmap[14] <= 16'd0; char_bitmap[15] <= 16'd0;
        end else begin
            dma_start_pulse <= 1'b0;
            
            dma_start_pulse <= 1'b0;
            
            // Set done sticky on DMA signal
            if (dma_done_in) dma_done_sticky <= 1'b1;

            if (avs_write) begin
                case (avs_address)
                    3'd0: reg_mode <= avs_writedata;
                    3'd1: begin
                         reg_global_ctrl[1:0] <= avs_writedata[1:0];
                        if (avs_writedata[2]) dma_start_pulse <= 1'b1;
                        if (avs_writedata[30]) dma_done_sticky <= 1'b0;
                    end
                    3'd2: reg_lut_addr <= avs_writedata;
                    3'd3: begin
                        reg_lut_data <= avs_writedata;
                        lut_mem[reg_lut_addr[7:0]] <= avs_writedata[7:0];
                    end
                    3'd4: reg_bitmap_addr <= avs_writedata;
                    3'd5: begin
                        reg_bitmap_data <= avs_writedata;
                        char_bitmap[reg_bitmap_addr[3:0]] <= avs_writedata[15:0];
                    end
                    3'd6: reg_frame_ptr <= avs_writedata;
                    default: ;
                endcase
            end
            
            // Read Valid Logic (1-cycle latency)
            avs_readdatavalid <= avs_read;
            
            // Register Read Data to align with Valid (T+1)
            // If read is asserted, capture the mux output for the next cycle
            if (avs_read) begin
                avs_readdata_reg <= read_data_mux;
            end
        end
    end

    // 960x540 (qHD) Timing Parameters
    parameter H_VISIBLE    = 960;
    parameter H_FRONT      = 48;
    parameter H_SYNC       = 32;
    parameter H_BACK       = 80;
    parameter H_TOTAL      = 1120;

    parameter V_VISIBLE    = 540;
    parameter V_FRONT      = 3;
    parameter V_SYNC       = 5;
    parameter V_BACK       = 15;
    parameter V_TOTAL      = 563;


    // Horizontal Counter
    // Horizontal Counter
    always @(posedge clk_pixel or negedge reset_n) begin
        if (!reset_n)
            h_cnt <= 12'd0;
        else if (h_cnt == H_TOTAL - 1)
            h_cnt <= 12'd0;
        else
            h_cnt <= h_cnt + 12'd1;
    end

    // Vertical Counter
    always @(posedge clk_pixel or negedge reset_n) begin
        if (!reset_n)
            v_cnt <= 12'd0;
        else if (h_cnt == H_TOTAL - 1) begin
            if (v_cnt == V_TOTAL - 1)
                v_cnt <= 12'd0;
            else
                v_cnt <= v_cnt + 12'd1;
        end
    end

    // Sync & DE Generation (Internal Wires for Alignment)
    wire visible = (h_cnt < H_VISIBLE && v_cnt < V_VISIBLE);
    wire hs_wire = (h_cnt >= (H_VISIBLE + H_FRONT) && h_cnt < (H_VISIBLE + H_FRONT + H_SYNC));
    wire vs_wire = (v_cnt >= (V_VISIBLE + V_FRONT) && v_cnt < (V_VISIBLE + V_FRONT + V_SYNC));

    // Pipeline Registers for DE and Data synchronization (clk_pixel domain)

    always @(posedge clk_pixel or negedge reset_n) begin
        if (!reset_n) begin
            hdmi_hs <= 1'b1;
            hdmi_vs <= 1'b1;
            hdmi_de <= 1'b0;
            visible_d1 <= 1'b0;
            hs_d1 <= 1'b0;
            vs_d1 <= 1'b0;
            vs_toggle <= 1'b0;
        end else begin
            // Shift pipeline
            visible_d1 <= visible;
            hs_d1 <= hs_wire;
            vs_d1 <= vs_wire;

            // Output registers (2-cycle delayed from internal counters)
            // Using Active-LOW syncs
            hdmi_hs <= ~hs_d1;
            hdmi_vs <= ~vs_d1;
            hdmi_de <= visible_d1;

            // VSync toggle for CDC (DMA needs this edge in 50MHz domain)
            if (vs_wire && !vs_d1) vs_toggle <= ~vs_toggle;
        end
    end

    // Shadow Pointer Update logic (CDC: vs_wire sync to clk)
    reg [2:0] vs_sync_sh;
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            vs_sync_sh <= 3'b0;
            shadow_ptr <= 32'h30000000;
        end else begin
            vs_sync_sh <= {vs_sync_sh[1:0], vs_wire};
            if (vs_sync_sh[1] && !vs_sync_sh[2]) begin // Sampling rising edge of VSync in clk domain
                shadow_ptr <= reg_frame_ptr;
            end
        end
    end

    // Pixel Data Generation Based on Mode
    reg  [23:0] pre_gamma_d;

    // LUT Logic (Apply only if Gamma Enable is 1)
    wire [7:0] gamma_r = lut_mem[pre_gamma_d[23:16]];
    wire [7:0] gamma_g = lut_mem[pre_gamma_d[15:8]];
    wire [7:0] gamma_b = lut_mem[pre_gamma_d[7:0]];

    wire [7:0] gray = (h_cnt < H_VISIBLE) ? ( (h_cnt * 255) / (H_VISIBLE-1) ) : 8'd0;
    wire grid_line = ( (h_cnt < H_VISIBLE) && (v_cnt < V_VISIBLE) ) && ( ((h_cnt % 60) == 0) || ((v_cnt % 60) == 0) );
    
    // Resolution-aware bar index calculation (8 bars)
    wire [2:0] bar_idx = (h_cnt < 1*H_VISIBLE/8) ? 3'd0 :
                         (h_cnt < 2*H_VISIBLE/8) ? 3'd1 :
                         (h_cnt < 3*H_VISIBLE/8) ? 3'd2 :
                         (h_cnt < 4*H_VISIBLE/8) ? 3'd3 :
                         (h_cnt < 5*H_VISIBLE/8) ? 3'd4 :
                         (h_cnt < 6*H_VISIBLE/8) ? 3'd5 :
                         (h_cnt < 7*H_VISIBLE/8) ? 3'd6 : 3'd7;
    wire [7:0] gray8_val = {bar_idx, 5'd0}; // Each step is 32

    // Character Tile Logic (16x16 Scaling 4x -> 64x64 Tile)
    wire [3:0] char_row_idx = v_cnt[5:2]; // 0-15
    wire [3:0] char_col_idx = h_cnt[5:2]; // 0-15
    wire [15:0] current_row_bits = char_bitmap[char_row_idx];
    wire char_pixel = current_row_bits[15 - char_col_idx]; // Leftmost bit is col 0
    
    // Dynamic Color for Character Rendering (Rainbow effect based on coordinates)
    wire [7:0] fancy_r = h_cnt[7:0] + v_cnt[7:0];
    wire [7:0] fancy_g = h_cnt[9:2];
    wire [7:0] fancy_b = v_cnt[9:2];
    wire [23:0] char_color = char_pixel ? {fancy_r, fancy_g, fancy_b} : 24'h000000;
    
    // Stream RD Enable: Read from FIFO only in visible area when mode is 8
    // Stream RD Enable: Read from FIFO only in visible area when mode is 8
    // FIFO read has 1-cycle latency, and hdmi_d adds another 1-cycle latency.
    // So we read at T=0 (visible), data valid at T=1, latch into hdmi_d at T=1, 
    // and hdmi_de goes high at T=2.
    assign stream_rd_en = (visible && v_cnt < V_VISIBLE && (reg_mode[3:0] == 4'd8));

    // Pixel Data Generation (Combinational based on H/V counters)
    always @(*) begin
        case (reg_mode[3:0])
            4'd0: pre_gamma_d = 24'hFF0000; // Red
            4'd1: pre_gamma_d = 24'h00FF00; // Green
            4'd2: pre_gamma_d = 24'h0000FF; // Blue
            4'd3: pre_gamma_d = {gray, gray, gray}; // Grayscale Ramp
            4'd4: pre_gamma_d = grid_line ? 24'hFFFFFF : 24'h000000; // Grid
            4'd5: pre_gamma_d = 24'hFFFFFF; // Solid White
            4'd6: pre_gamma_d = {gray8_val, gray8_val, gray8_val}; // 8-level Gray Scale
            4'd7: pre_gamma_d = char_color; // Character Tile 4x
            4'd8: pre_gamma_d = stream_data_in; // DMA Stream
            default: pre_gamma_d = 24'hFFFFFF; // White
        endcase
    end

    // Final Output Stage (clk_pixel Domain)
    always @(posedge clk_pixel or negedge reset_n) begin
        if (!reset_n) begin
            hdmi_d <= 24'h000000;
        end else begin
            if (visible_d1) begin
                // If Gamma is enabled (Bit 0 of global ctrl)
                if (reg_global_ctrl[0])
                    hdmi_d <= {gamma_r, gamma_g, gamma_b};
                else
                    hdmi_d <= pre_gamma_d;
            end else begin
                hdmi_d <= 24'h000000; // Blanking
            end
        end
    end

endmodule
