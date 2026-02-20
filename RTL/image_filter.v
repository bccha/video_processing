`timescale 1ns/1ps

module image_filter #(
    parameter DATA_WIDTH = 24, // RGB 888 (8 bits per channel)
    parameter LINE_LENGTH = 1120 // H_TOTAL for 960x540
)(
    input  wire                  clk,
    input  wire                  reset_n,
    
    // Control
    // 0: Bypass, 1: Grayscale, 
    // 2: Blur (Gray), 3: Blur (Color), 
    // 4: Edge (Gray), 5: Edge (Color lines on black)
    // 6: Emboss (Gray), 7: Sharpen (Color)
    input  wire  [3:0]           filter_mode, 
    
    // Input Video Stream
    input  wire  [DATA_WIDTH-1:0] din,
    input  wire                  hs_in,
    input  wire                  vs_in,
    input  wire                  de_in,
    
    // Output Video Stream (Matched Delay: 3 Clocks)
    output wire [DATA_WIDTH-1:0] dout,
    output wire                  hs_out,
    output wire                  vs_out,
    output wire                  de_out
);

    // ==========================================
    // 0. Clock Domain Crossing (CDC) Synchronizer
    // ==========================================
    // filter_mode comes from 50MHz CSR domain, but this module runs on clk_pixel (37.8MHz).
    // We MUST synchronize it using a 2-stage D-FlipFlop to prevent metastability.
    reg [3:0] filter_mode_sync1;
    reg [3:0] filter_mode_sync2;
    
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            filter_mode_sync1 <= 4'd0;
            filter_mode_sync2 <= 4'd0;
        end else begin
            filter_mode_sync1 <= filter_mode;       // 1st stage: Catch the asynchronous signal
            filter_mode_sync2 <= filter_mode_sync1; // 2nd stage: Stable synchronized signal
        end
    end

    wire [3:0] safe_filter_mode = filter_mode_sync2;

    // ==========================================
    // 1. Line Buffers (Generate 3 rows)
    // ==========================================
    wire [DATA_WIDTH-1:0] row0_data = din;
    wire [DATA_WIDTH-1:0] row1_data;
    wire [DATA_WIDTH-1:0] row2_data;
    
    line_buffer #(.DATA_WIDTH(DATA_WIDTH), .LINE_LENGTH(LINE_LENGTH)) u_lb1 (
        .clk(clk), .reset_n(reset_n), .wr_en(1'b1), .din(row0_data), .dout(row1_data)
    );
    
    line_buffer #(.DATA_WIDTH(DATA_WIDTH), .LINE_LENGTH(LINE_LENGTH)) u_lb2 (
        .clk(clk), .reset_n(reset_n), .wr_en(1'b1), .din(row1_data), .dout(row2_data)
    );

    // ==========================================
    // 2. 3x3 Window Generation (24-bit RGB Delay)
    // ==========================================
    wire [DATA_WIDTH-1:0] rgb02 = row2_data; // Oldest Row
    wire [DATA_WIDTH-1:0] rgb12 = row1_data; // Center Row
    wire [DATA_WIDTH-1:0] rgb22 = row0_data; // Newest Row
    
    wire [DATA_WIDTH-1:0] rgb01, rgb11, rgb21;
    delay_line #(.WIDTH(DATA_WIDTH), .STAGES(1)) d01(.clk(clk), .reset_n(reset_n), .din(rgb02), .dout(rgb01));
    delay_line #(.WIDTH(DATA_WIDTH), .STAGES(1)) d11(.clk(clk), .reset_n(reset_n), .din(rgb12), .dout(rgb11));
    delay_line #(.WIDTH(DATA_WIDTH), .STAGES(1)) d21(.clk(clk), .reset_n(reset_n), .din(rgb22), .dout(rgb21));
    
    wire [DATA_WIDTH-1:0] rgb00, rgb10, rgb20;
    delay_line #(.WIDTH(DATA_WIDTH), .STAGES(1)) d00(.clk(clk), .reset_n(reset_n), .din(rgb01), .dout(rgb00));
    delay_line #(.WIDTH(DATA_WIDTH), .STAGES(1)) d10(.clk(clk), .reset_n(reset_n), .din(rgb11), .dout(rgb10));
    delay_line #(.WIDTH(DATA_WIDTH), .STAGES(1)) d20(.clk(clk), .reset_n(reset_n), .din(rgb21), .dout(rgb20));

    // Internal Intensity (Gray) calculation macro
    function [7:0] rgb2gray;
        input [23:0] rgb;
        begin
            rgb2gray = (rgb[23:16] >> 2) + (rgb[15:8] >> 1) + (rgb[7:0] >> 2);
        end
    endfunction

    // ==========================================
    // 3. Filter Computations (Submodules)
    // ==========================================
    wire [7:0] blur_r, blur_g, blur_b, blur_gray;
    wire [7:0] edge_mag;
    wire [7:0] emboss_gray;
    wire [7:0] sharp_r, sharp_g, sharp_b;

    // Blur computations
    filter_blur #(.DATA_WIDTH(DATA_WIDTH)) u_blur (
        .clk    (clk),
        .reset_n(reset_n),
        .rgb00(rgb00), .rgb01(rgb01), .rgb02(rgb02),
        .rgb10(rgb10), .rgb11(rgb11), .rgb12(rgb12),
        .rgb20(rgb20), .rgb21(rgb21), .rgb22(rgb22),
        .blur_r (blur_r),
        .blur_g (blur_g),
        .blur_b (blur_b),
        .blur_gray(blur_gray)
    );

    // Edge computations
    filter_edge #(.DATA_WIDTH(DATA_WIDTH)) u_edge (
        .clk    (clk),
        .reset_n(reset_n),
        .rgb00(rgb00), .rgb01(rgb01), .rgb02(rgb02),
        .rgb10(rgb10), .rgb11(rgb11), .rgb12(rgb12),
        .rgb20(rgb20), .rgb21(rgb21), .rgb22(rgb22),
        .edge_mag(edge_mag)
    );

    // Emboss computations
    filter_emboss #(.DATA_WIDTH(DATA_WIDTH)) u_emboss (
        .clk    (clk),
        .reset_n(reset_n),
        .rgb00(rgb00), .rgb01(rgb01), .rgb02(rgb02),
        .rgb10(rgb10), .rgb11(rgb11), .rgb12(rgb12),
        .rgb20(rgb20), .rgb21(rgb21), .rgb22(rgb22),
        .emboss_gray(emboss_gray)
    );

    // Sharpen computations
    filter_sharpen #(.DATA_WIDTH(DATA_WIDTH)) u_sharpen (
        .clk    (clk),
        .reset_n(reset_n),
        .rgb00(rgb00), .rgb01(rgb01), .rgb02(rgb02),
        .rgb10(rgb10), .rgb11(rgb11), .rgb12(rgb12),
        .rgb20(rgb20), .rgb21(rgb21), .rgb22(rgb22),
        .sharp_r (sharp_r),
        .sharp_g (sharp_g),
        .sharp_b (sharp_b)
    );

    // ==========================================
    // 4. Center Pixel Delay Match (2 Clocks)
    // ==========================================
    reg [23:0] center_rgb_st1; 
    reg [23:0] center_rgb_st2;
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            center_rgb_st1 <= 24'd0;
            center_rgb_st2 <= 24'd0;
        end else begin
            center_rgb_st1 <= rgb11; 
            center_rgb_st2 <= center_rgb_st1;
        end
    end

    // ==========================================
    // 5. Output Selection & Control Sync Delay
    // ==========================================
    // Computation takes 1 clk (horizontal window alignment) + 2 clk (pipeline) = 3 clocks.
    delay_line #(.WIDTH(3), .STAGES(3)) u_sync_delay (
        .clk(clk), .reset_n(reset_n), 
        .din({vs_in, hs_in, de_in}), .dout({vs_out, hs_out, de_out})
    );
    
    // Final RGB generation based on mode
    wire [7:0] center_gray = rgb2gray(center_rgb_st2);
    
    // Edge color lines on black background: edge_mag * color_intensity
    wire is_edge = (edge_mag > 8'd40); // Threshold
    wire [23:0] color_edge = is_edge ? center_rgb_st2 : 24'd0;

    assign dout = (safe_filter_mode == 4'd7) ? {sharp_r, sharp_g, sharp_b} :                  // Mode 7: Color Sharpen
                  (safe_filter_mode == 4'd6) ? {emboss_gray, emboss_gray, emboss_gray} :     // Mode 6: Grayscale Emboss
                  (safe_filter_mode == 4'd5) ? color_edge :                                  // Mode 5: Color Edge
                  (safe_filter_mode == 4'd4) ? {edge_mag, edge_mag, edge_mag} :              // Mode 4: Grayscale Edge
                  (safe_filter_mode == 4'd3) ? {blur_r, blur_g, blur_b} :                    // Mode 3: Color Blur
                  (safe_filter_mode == 4'd2) ? {blur_gray, blur_gray, blur_gray} :           // Mode 2: Grayscale Blur
                  (safe_filter_mode == 4'd1) ? {center_gray, center_gray, center_gray} :     // Mode 1: Grayscale Original
                  center_rgb_st2;                                                            // Mode 0: Bypass Full Color



endmodule

