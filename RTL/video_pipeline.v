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
    
    wire        vs_toggle_raw; // New VSync Toggle from Sync Gen

    // Internal signals (Missing declarations added)
    wire [31:0] shadow_ptr;
    wire [8:0]  fifo_used;
    wire        fifo_wr_en;
    wire [31:0] fifo_wr_data;
    wire        fifo_full;
    wire        fifo_rd_en;
    wire [31:0] fifo_rd_data;
    wire        fifo_empty;
    wire        dma_busy;
    wire        dma_en;
    wire [31:0] reg_mode;
    wire        dma_done_50;
    // wire        dma_start_74; // Removed, using direct connection
    // wire        dma_cont_74;  // Removed, using direct connection

    // Pipeline status (Internal)
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

    // 3. Simple Dual Clock FIFO (Verilog Only)
    simple_dcfifo #(
        .DATA_WIDTH(32),
        .ADDR_WIDTH(9) // 512 depth
    ) u_simple_fifo (
        .wrclk   (clk_50),
        .data    (fifo_wr_data),
        .wrreq   (fifo_wr_en),
        .wrusedw (fifo_used),
        .wrfull  (fifo_full),
        
        .rdclk   (clk_hdmi),
        .rdreq   (fifo_rd_en),
        .q       (fifo_rd_data),
        .rdempty (fifo_empty)
    );

    // 4. HDMI Sync & Pattern Generator
    hdmi_sync_gen u_hdmi_sync (
        .clk               (clk_50),           // CSR Clock
        .clk_pixel         (clk_hdmi),         // Pixel Clock
        .reset_n           (reset_n),
        .hdmi_d            (hdmi_d),
        .hdmi_de           (hdmi_de),
        .hdmi_hs           (hdmi_hs),
        .hdmi_vs           (hdmi_vs),
        
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
        .dma_enable_out    (dma_en),
        
        .dma_busy          (dma_busy),
        .dma_done_in       (dma_done_direct),
        .dma_start_out     (dma_start_direct),
        .dma_cont_en_out   (dma_cont_direct),
        .vs_toggle         (vs_toggle_raw)
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
