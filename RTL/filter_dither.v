`timescale 1ns/1ps

module filter_dither #(
    parameter DATA_WIDTH = 24
)(
    input  wire                  clk,
    input  wire                  reset_n,
    
    // Control Enable
    input  wire                  temporal_en,
    
    // Pixel Input (Latency = 1 relative to start of row)
    input  wire [DATA_WIDTH-1:0] pixel_in,
    input  wire [11:0]           x_coord,
    input  wire [11:0]           y_coord,
    
    // Output (Delayed by 2 clocks to match pipeline)
    output reg  [DATA_WIDTH-1:0] pixel_out
);

    // ==========================================
    // 1. 4x4 Bayer Matrix LUT (0~15)
    // ==========================================
    // [ 0,  8,  2, 10 ]
    // [12,  4, 14,  6 ]
    // [ 3, 11,  1,  9 ]
    // [15,  7, 13,  5 ]
    
    // Temporal Dithering: Shift starting point per frame
    // Temporal Dithering: Shift starting point per frame
    reg [3:0] frame_cnt;
    reg [11:0] y_coord_prev;
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            frame_cnt <= 0;
            y_coord_prev <= 0;
        end else begin
            y_coord_prev <= y_coord;
            // Increment exactly once per frame when y_coord resets to 0
            if (y_coord == 0 && y_coord_prev != 0) begin
                frame_cnt <= frame_cnt + 1;
            end
        end
    end

    // Scramble the 4-bit frame counter into 2D (x, y) offsets
    // This avoids diagonal 'scrolling' artifacts by jumping pseudorandomly 
    // across all 16 matrix positions over 16 frames.
    wire [1:0] x_offset = {frame_cnt[0], frame_cnt[2]};
    wire [1:0] y_offset = {frame_cnt[1], frame_cnt[3]};

    wire [1:0] x_idx = temporal_en ? (x_coord[1:0] + x_offset) : x_coord[1:0];
    wire [1:0] y_idx = temporal_en ? (y_coord[1:0] + y_offset) : y_coord[1:0];

    // Decorrelate RGB channels by adding spatial offsets
    // R: no offset, G: (x+1, y+2), B: (x+2, y+1)
    wire [1:0] x_idx_r = x_idx;
    wire [1:0] y_idx_r = y_idx;
    
    wire [1:0] x_idx_g = x_idx + 2'd1;
    wire [1:0] y_idx_g = y_idx + 2'd2;
    
    wire [1:0] x_idx_b = x_idx + 2'd2;
    wire [1:0] y_idx_b = y_idx + 2'd1;

    // Bayer ROM lookups
    function [3:0] bayer_rom;
        input [3:0] idx; // {y, x}
        begin
            case (idx)
                4'b00_00: bayer_rom = 4'd0;
                4'b00_01: bayer_rom = 4'd8;
                4'b00_10: bayer_rom = 4'd2;
                4'b00_11: bayer_rom = 4'd10;
                4'b01_00: bayer_rom = 4'd12;
                4'b01_01: bayer_rom = 4'd4;
                4'b01_10: bayer_rom = 4'd14;
                4'b01_11: bayer_rom = 4'd6;
                4'b10_00: bayer_rom = 4'd3;
                4'b10_01: bayer_rom = 4'd11;
                4'b10_10: bayer_rom = 4'd1;
                4'b10_11: bayer_rom = 4'd9;
                4'b11_00: bayer_rom = 4'd15;
                4'b11_01: bayer_rom = 4'd7;
                4'b11_10: bayer_rom = 4'd13;
                4'b11_11: bayer_rom = 4'd5;
            endcase
        end
    endfunction

    wire [3:0] bayer_val_r = bayer_rom({y_idx_r, x_idx_r});
    wire [3:0] bayer_val_g = bayer_rom({y_idx_g, x_idx_g});
    wire [3:0] bayer_val_b = bayer_rom({y_idx_b, x_idx_b});

    // ==========================================
    // Stage 1: Noise Addition (9-bit to prevent overflow)
    // ==========================================
    wire [7:0] r_in = pixel_in[23:16];
    wire [7:0] g_in = pixel_in[15:8];
    wire [7:0] b_in = pixel_in[7:0];

    reg [8:0] sum_r, sum_g, sum_b;
    reg [11:0] x_coord_st1; // Propagate x_coord for split screen
    reg [7:0] r_in_st1, g_in_st1, b_in_st1;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            sum_r <= 0; sum_g <= 0; sum_b <= 0;
            x_coord_st1 <= 0;
            r_in_st1 <= 0; g_in_st1 <= 0; b_in_st1 <= 0;
        end else begin
            // Add full 4-bit Bayer noise (0-15) directly to 8-bit color
            // Using separated RGB Bayer offsets
            sum_r <= {1'b0, r_in} + {5'b0, bayer_val_r};
            sum_g <= {1'b0, g_in} + {5'b0, bayer_val_g};
            sum_b <= {1'b0, b_in} + {5'b0, bayer_val_b};
            
            x_coord_st1 <= x_coord;
            
            // Pass through originals for the left screen
            r_in_st1 <= r_in;
            g_in_st1 <= g_in;
            b_in_st1 <= b_in;
        end
    end

    // ==========================================
    // Stage 2: Clamping, 4-bit Truncation, and Cutoff
    // ==========================================
    // Truncate lower 4 bits
    wire [7:0] r_clamp = (sum_r > 255) ? 8'd255 : sum_r[7:0];
    wire [7:0] g_clamp = (sum_g > 255) ? 8'd255 : sum_g[7:0];
    wire [7:0] b_clamp = (sum_b > 255) ? 8'd255 : sum_b[7:0];

    // Truncate masks (Lower 4 bits zeroed out)
    localparam TRUNC_MASK = 8'hF0;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            pixel_out <= 24'd0;
        end else begin
            if (x_coord_st1 < 12'd480) begin
                // Left Screen: Original Truncated for dark areas
                pixel_out[23:16] <= (r_in_st1 >= 8'h10) ? r_in_st1 : (r_in_st1 & TRUNC_MASK);
                pixel_out[15:8]  <= (g_in_st1 >= 8'h10) ? g_in_st1 : (g_in_st1 & TRUNC_MASK);
                pixel_out[7:0]   <= (b_in_st1 >= 8'h10) ? b_in_st1 : (b_in_st1 & TRUNC_MASK);
            end else begin
                // Right Screen: Dithered & Truncated for dark areas
                pixel_out[23:16] <= (r_in_st1 >= 8'h10) ? r_in_st1 : (r_clamp & TRUNC_MASK);
                pixel_out[15:8]  <= (g_in_st1 >= 8'h10) ? g_in_st1 : (g_clamp & TRUNC_MASK);
                pixel_out[7:0]   <= (b_in_st1 >= 8'h10) ? b_in_st1 : (b_clamp & TRUNC_MASK);
            end
        end
    end

endmodule
