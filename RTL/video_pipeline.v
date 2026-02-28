`timescale 1ns/1ps

module video_pipeline (
    // Clocks & Reset
    input  wire         clk_50,             // DMA & FIFO Write Clock
    input  wire         clk_hdmi,           // HDMI Pixel Clock (~37.8 MHz)
    input  wire         reset_n,

    // Avalon-MM Master Interface (to DDR3)
    input  wire         m_waitrequest,
    input  wire [31:0]  m_readdata,
    input  wire         m_readdatavalid,
    output wire [31:0]  m_address,
    output wire         m_read,
    output wire [7:0]   m_burstcount,

    // Avalon-MM Slave Interface (Control from Nios II)
    input  wire [2:0]   s_address,
    input  wire         s_read,
    input  wire         s_write,
    input  wire [31:0]  s_writedata,
    output wire [31:0]  s_readdata,
    output wire         s_readdatavalid,

    // Avalon-MM Slave Interface for Color Matrix (from Nios II / ARM Linux)
    // Addr 0: Control[0]=matrix_en, Addr 1~9: C00~C22 (12-bit signed, x1024)
    input  wire [3:0]   cm_s_address,
    input  wire         cm_s_read,
    input  wire         cm_s_write,
    input  wire [31:0]  cm_s_writedata,
    output wire [31:0]  cm_s_readdata,

    // HDMI Physical Output Signals
    output wire [23:0]  hdmi_d,
    output wire         hdmi_de,
    output wire         hdmi_hs,
    output wire         hdmi_vs,
    // Debug LEDs
    output wire [7:0]   debug_leds
);

    // Internal connections 
    // hdmi_d, hdmi_de, hdmi_hs, hdmi_vs are output ports
    
    wire        vs_toggle_raw; // VSync Toggle from Sync Gen

    // ==========================================
    // Internal Wires & Control Signals
    // ==========================================
    
    // Control Registers (from Sync Gen)
    wire [31:0] shadow_ptr;
    wire [31:0] reg_mode;
    wire [31:0] reg_filter_config;
    
    // DMA Status & Control
    wire        dma_busy;
    wire        dma_en;
    wire        dma_done_50;

    // Cross-Domain (DC) FIFO Signals
    wire [11:0] fifo_used;
    wire        fifo_wr_en;
    wire [31:0] fifo_wr_data;
    wire        fifo_full;
    wire        fifo_rd_en;
    wire [31:0] fifo_rd_data;
    wire        fifo_empty;

    // Pipeline status (Internal Debug)
    wire [7:0]  pipeline_debug;
    
    // 1. CDC (V-Sync, Start, Cont, Done)
    // 1.1 V-Sync: 74MHz -> 50MHz (Using Toggle from Sync Gen)
    reg [2:0] vsync_toggle_sync_50;
    always @(posedge clk_50 or negedge reset_n) begin
        if (!reset_n) vsync_toggle_sync_50 <= 3'b0;
        else vsync_toggle_sync_50 <= {vsync_toggle_sync_50[1:0], vs_toggle_raw};
    end
    wire vsync_edge_sync = vsync_toggle_sync_50[2] ^ vsync_toggle_sync_50[1]; // Edge Detect

    // 1.2 Start & Cont: 50MHz -> 50MHz (Direct Connection)
    // No CDC needed as both CSR (Nios) and DMA Master are on clk_50
    wire dma_start_direct;
    wire dma_cont_direct;
    
    // 1.3 Done: 50MHz -> 50MHz (Direct Connection)
    wire dma_done_direct;
    assign dma_done_direct = dma_done_50;

    // 2. Video DMA Master (Reads from DDR3)
    video_dma_master #(
        .H_RES(960),
        .V_RES(540)
    ) u_dma_master (
        .clk               (clk_50),
        .reset_n           (reset_n),
        .start_addr        (shadow_ptr),
        .dma_start         (dma_start_direct),
        .dma_cont_en       (dma_cont_direct),
        .dma_done          (dma_done_50),
        .vsync_edge        (vsync_edge_sync),
        .m_waitrequest     (m_waitrequest),
        .m_readdata        (m_readdata),
        .m_readdatavalid   (m_readdatavalid),
        .m_address         (m_address),
        .m_read            (m_read),
        .m_burstcount      (m_burstcount),
        .fifo_used         (fifo_used),
        .fifo_wr_en        (fifo_wr_en),
        .fifo_wr_data      (fifo_wr_data),
        .busy              (dma_busy)
    );

    // 3. DC FIFO (Megafunction / Simulation Model)
`ifdef COCOTB_SIM
    simple_dcfifo #(
        .DATA_WIDTH(32),
        .ADDR_WIDTH(10) // Match typical MegaWizard depth (e.g., 1024)
    ) u_dc_fifo (
        .data    (fifo_wr_data),
        .rdclk   (clk_hdmi),
        .rdreq   (fifo_rd_en),
        .wrclk   (clk_50),
        .wrreq   (fifo_wr_en),
        .q       (fifo_rd_data),
        .rdempty (fifo_empty),
        .wrfull  (fifo_full),
        .wrusedw (fifo_used)
    );
`else
    DC_FIFO u_dc_fifo (
        .aclr    (~reset_n | vsync_edge_sync),
        .data    (fifo_wr_data),
        .rdclk   (clk_hdmi),
        .rdreq   (fifo_rd_en),
        .wrclk   (clk_50),
        .wrreq   (fifo_wr_en),
        .q       (fifo_rd_data),
        .rdempty (fifo_empty),
        .rdusedw (), // Not used
        .wrfull  (fifo_full),
        .wrusedw (fifo_used)
    );
`endif

    // Internal wires for raw HDMI signals from Sync Gen
    wire [23:0] raw_hdmi_d;
    wire        raw_hdmi_de;
    wire        raw_hdmi_hs;
    wire        raw_hdmi_vs;

    // 4. HDMI Sync & Pattern Generator
    hdmi_sync_gen u_hdmi_sync (
        .clk               (clk_50),           // CSR Clock
        .clk_pixel         (clk_hdmi),         // Pixel Clock
        .reset_n           (reset_n),
        .hdmi_d            (raw_hdmi_d),
        .hdmi_de           (raw_hdmi_de),
        .hdmi_hs           (raw_hdmi_hs),
        .hdmi_vs           (raw_hdmi_vs),
        
        .avs_address       (s_address),
        .avs_read          (s_read),
        .avs_write         (s_write),
        .avs_writedata     (s_writedata),
        .avs_readdata      (s_readdata),
        .avs_readdatavalid (s_readdatavalid),
        
        .stream_data_in    (fifo_rd_data[23:0]),
        .stream_rd_en      (fifo_rd_en),
        
        .shadow_ptr_out    (shadow_ptr),
        .reg_mode_out      (reg_mode),
        .reg_filter_config_out (reg_filter_config),
        .dma_enable_out    (dma_en),
        
        .dma_busy          (dma_busy),
        .dma_done_in       (dma_done_direct),
        .dma_start_out     (dma_start_direct),
        .dma_cont_en_out   (dma_cont_direct),
        .vs_toggle         (vs_toggle_raw)
    );

    // 5. Image Processing Filter (Blur / Edge)
    wire [3:0] current_filter_mode = reg_mode[7:4]; 

    wire [23:0] img_filter_dout;
    wire        img_filter_hs;
    wire        img_filter_vs;
    wire        img_filter_de;

    image_filter #(
        .DATA_WIDTH(24),
        .LINE_LENGTH(1120) // 960x540 H_TOTAL
    ) u_img_filter (
        .clk         (clk_hdmi),       // Filter runs on Pixel Clock!
        .reset_n     (reset_n),
        .filter_mode (current_filter_mode),
        // temporal_en / dither_2bit_en now handled by filter_dither post color_matrix
        
        // Pixel Input from Sync Gen
        .din         (raw_hdmi_d),
        .hs_in       (raw_hdmi_hs),
        .vs_in       (raw_hdmi_vs),
        .de_in       (raw_hdmi_de),
        
        // Output to intermediate wires
        .dout        (img_filter_dout),
        .hs_out      (img_filter_hs),
        .vs_out      (img_filter_vs),
        .de_out      (img_filter_de)
    );

    // 6. De-Gamma (sRGB -> Linear, ideal gamma 2.2 inverse LUT)
    // reg_filter_config[1]: degamma_en
    wire degamma_en = reg_filter_config[1] | cm_matrix_en; // auto-enable when matrix is on

    wire [35:0] degamma_dout;
    wire        degamma_hs, degamma_vs, degamma_de;

    filter_degamma u_degamma (
        .clk        (clk_hdmi),
        .reset_n    (reset_n),
        .degamma_en (degamma_en),
        .din        (img_filter_dout),
        .hs_in      (img_filter_hs),
        .vs_in      (img_filter_vs),
        .de_in      (img_filter_de),
        .dout       (degamma_dout),
        .hs_out     (degamma_hs),
        .vs_out     (degamma_vs),
        .de_out     (degamma_de)
    );

    // 7. 3x3 Color Gamut Matrix
    wire [35:0] cm_dout;
    wire        cm_hs, cm_vs, cm_de;
    wire        cm_matrix_en; // exposed from filter_color_matrix (pixel clock domain)

    filter_color_matrix u_color_matrix (
        .clk         (clk_hdmi),
        .clk_csr     (clk_50),
        .reset_n     (reset_n),
        // Avalon-MM Slave
        .s_address   (cm_s_address),
        .s_write     (cm_s_write),
        .s_writedata (cm_s_writedata),
        .s_read      (cm_s_read),
        .s_readdata  (cm_s_readdata),
        // Video In from De-Gamma
        .din         (degamma_dout),
        .hs_in       (degamma_hs),
        .vs_in       (degamma_vs),
        .de_in       (degamma_de),
        // Video Out to Gamma
        .dout        (cm_dout),
        .hs_out      (cm_hs),
        .vs_out      (cm_vs),
        .de_out      (cm_de),
        .matrix_en_out (cm_matrix_en)
    );

    // 8. Gamma Re-encoding (Linear -> Display, inverse of degamma)
    // Correctly placed after the color matrix (which works in linear space)
    wire gamma_en = reg_filter_config[2] | cm_matrix_en; // auto-enable when matrix is on

    wire [23:0] gamma_dout;
    wire        gamma_hs, gamma_vs, gamma_de;

    filter_gamma u_gamma (
        .clk      (clk_hdmi),
        .reset_n  (reset_n),
        .gamma_en (gamma_en),
        .din      (cm_dout),
        .hs_in    (cm_hs),
        .vs_in    (cm_vs),
        .de_in    (cm_de),
        .dout     (gamma_dout),
        .hs_out   (gamma_hs),
        .vs_out   (gamma_vs),
        .de_out   (gamma_de)
    );

    // 9. Bayer + Temporal Dither (Post Color-Matrix and Gamma, Pre Error-Diffusion)
    // Correct position: after gamut correction, before quantization
    // Pixel coordinates tracked from cm_de/cm_hs/cm_vs
    reg [11:0] dith_x_cnt;
    reg [11:0] dith_y_cnt;
    reg        dith_de_d;
    always @(posedge clk_hdmi or negedge reset_n) begin
        if (!reset_n) begin
            dith_x_cnt <= 0; dith_y_cnt <= 0; dith_de_d <= 0;
        end else begin
            dith_de_d <= gamma_de;
            if (gamma_de)       dith_x_cnt <= dith_x_cnt + 1;
            else if (!gamma_hs) dith_x_cnt <= 0;
            if (!gamma_vs)                                   dith_y_cnt <= 0;
            else if (dith_de_d && !gamma_de)                 dith_y_cnt <= dith_y_cnt + 1;
        end
    end

    wire [23:0] dither_dout;
    wire        dither_hs, dither_vs, dither_de;

    filter_dither #(.DATA_WIDTH(24)) u_post_matrix_dither (
        .clk            (clk_hdmi),
        .reset_n        (reset_n),
        .temporal_en    (reg_mode[8]),
        .dither_2bit_en (reg_mode[9]),
        .pixel_in       (gamma_dout),
        .x_coord        (dith_x_cnt),
        .y_coord        (dith_y_cnt),
        .pixel_out      (dither_dout)
    );
    // Delay sync signals to match filter_dither 2-clock pipeline
    delay_line #(.WIDTH(3), .STAGES(2)) u_dith_sync (
        .clk(clk_hdmi), .reset_n(reset_n),
        .din({gamma_vs, gamma_hs, gamma_de}),
        .dout({dither_vs, dither_hs, dither_de})
    );

    // 9. Floyd-Steinberg Error Diffusion Filter
    wire error_diffusion_en = reg_filter_config[0];
    wire dither_en          = reg_filter_config[3]; // bit[3]: Global Dither Enable
    wire [7:0] dither_threshold = reg_filter_config[15:8];

    filter_error_diffusion #(
        .DATA_WIDTH(24)
    ) u_err_diff (
        .clk                (clk_hdmi),
        .reset_n            (reset_n),
        .error_diffusion_en (error_diffusion_en),
        .dither_threshold   (dither_threshold),
        
        // Mux: Choose between Dithered or Gamma-encoded (direct) pixels
        .din                (dither_en ? dither_dout : gamma_dout),
        .hs_in              (dither_en ? dither_hs   : gamma_hs),
        .vs_in              (dither_en ? dither_vs   : gamma_vs),
        .de_in              (dither_en ? dither_de   : gamma_de),
        
        // Output to Physical HDMI Pins
        .dout               (hdmi_d),
        .hs_out             (hdmi_hs),
        .vs_out             (hdmi_vs),
        .de_out             (hdmi_de)
    );

    // Debug LED Logic (Stretched Pulses for visibility)
    // dma_start_pulse is 1 clock wide. We need to stretch it or toggle it to see on LED.
    // Let's just output raw signals, user can use logic analyzer or scope if needed, 
    // or trust the toggle nature of some signals.
    // Debug LED Logic (Modified for Data Path Debugging)
    // [0] FIFO Write Enable (Pulse) - Should flicker if data arrives
    // [1] FIFO Read Enable (Pulse) - Should flicker if HDMI reads
    // [2] FIFO Used MSB (Wait, local signal fifo_used is 9-bit) - Is FIFO filling up?
    // [3] FIFO Empty (Active High)
    // [4] DMA Start (Pulse 50MHz)
    // [5] DMA Start Toggle (74MHz)
    // [6] DMA Done (Toggle)
    // [7] V-Sync Edge
    
    // We need to bring out internal signals from dma_master or assume them from assignments
    // In video_pipeline, fifo_wr_en comes from u_dma_master.
    
    assign debug_leds[0] = fifo_wr_en;      // Data arriving from DDR3?
    assign debug_leds[1] = fifo_rd_en;      // HDMI consuming data?
    assign debug_leds[2] = fifo_used[8];    // FIFO Half Full? (If 1, overflow risk)
    assign debug_leds[3] = fifo_empty;      // Is FIFO empty? (Should be 0 during play)
    assign debug_leds[4] = dma_start_direct;  
    assign debug_leds[5] = dma_cont_direct; 
    assign debug_leds[6] = dma_done_direct; // Keep this! 
    assign debug_leds[7] = vsync_edge_sync;

endmodule
