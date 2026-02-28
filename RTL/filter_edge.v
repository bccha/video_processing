module filter_edge #(
    parameter DATA_WIDTH = 24
)(
    input  wire clk,
    input  wire reset_n,
    
    // 3x3 Window (RGB)
    input  wire [DATA_WIDTH-1:0] rgb00, rgb01, rgb02,
    input  wire [DATA_WIDTH-1:0] rgb10, rgb11, rgb12,
    input  wire [DATA_WIDTH-1:0] rgb20, rgb21, rgb22,
    
    // Outputs (Delayed by 2 clocks)
    output reg  [7:0] edge_mag
);

    // Internal Intensity (Gray) calculation macro
    function [7:0] rgb2gray;
        input [23:0] rgb;
        begin
            rgb2gray = (rgb[23:16] >> 2) + (rgb[15:8] >> 1) + (rgb[7:0] >> 2);
        end
    endfunction

    // ==========================================
    // Stage 1: Sobel Masks Calculation
    // ==========================================
    reg signed [10:0] gx1, gx2, gy1, gy2;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            gx1 <= 0; gx2 <= 0; gy1 <= 0; gy2 <= 0;
        end else begin
            // Edge (Sobel) - Only needs Intensity (Grayscale)
            // Gx = [-1 0 1; -2 0 2; -1 0 1]
            gx1 <= rgb2gray(rgb02) + (rgb2gray(rgb12) << 1) + rgb2gray(rgb22); // Positives
            gx2 <= rgb2gray(rgb00) + (rgb2gray(rgb10) << 1) + rgb2gray(rgb20); // Negatives
            
            // Gy = [1 2 1; 0 0 0; -1 -2 -1]
            gy1 <= rgb2gray(rgb00) + (rgb2gray(rgb01) << 1) + rgb2gray(rgb02); // Positives
            gy2 <= rgb2gray(rgb20) + (rgb2gray(rgb21) << 1) + rgb2gray(rgb22); // Negatives
        end
    end

    // ==========================================
    // Stage 2: Magnitude Approximation
    // ==========================================
    wire signed [11:0] gx_diff = gx1 - gx2;
    wire signed [11:0] gy_diff = gy1 - gy2;
    wire [10:0] abs_gx = (gx_diff[11]) ? -gx_diff : gx_diff;
    wire [10:0] abs_gy = (gy_diff[11]) ? -gy_diff : gy_diff;
    wire [11:0] sum_mag = abs_gx + abs_gy;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            edge_mag <= 0;
        end else begin
            edge_mag <= (sum_mag > 255) ? 8'd255 : sum_mag[7:0];
        end
    end

endmodule
