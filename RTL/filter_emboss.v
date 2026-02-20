module filter_emboss #(
    parameter DATA_WIDTH = 24
)(
    input  wire clk,
    input  wire reset_n,
    
    // 3x3 Window (RGB)
    input  wire [DATA_WIDTH-1:0] rgb00, rgb01, rgb02,
    input  wire [DATA_WIDTH-1:0] rgb10, rgb11, rgb12,
    input  wire [DATA_WIDTH-1:0] rgb20, rgb21, rgb22,
    
    // Output (Delayed by 2 clocks to match pipeline)
    output reg  [7:0] emboss_gray
);

    // Internal Intensity (Gray) calculation macro
    function [7:0] rgb2gray;
        input [23:0] rgb;
        begin
            rgb2gray = (rgb[23:16] >> 2) + (rgb[15:8] >> 1) + (rgb[7:0] >> 2);
        end
    endfunction

    // ==========================================
    // Stage 1: Emboss Mask Calculation
    // Emboss Mask (Diagonal):
    // [ -2 -1  0 ]
    // [ -1  1  1 ]
    // [  0  1  2 ]
    // ==========================================
    reg signed [12:0] emboss_sum;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            emboss_sum <= 0;
        end else begin
            // Positives: rgb11 + rgb12 + rgb21 + 2*rgb22
            // Negatives: -2*rgb00 - rgb01 - rgb10
            emboss_sum <= 
                (rgb2gray(rgb11) + rgb2gray(rgb12) + rgb2gray(rgb21) + (rgb2gray(rgb22) << 1)) -
                ((rgb2gray(rgb00) << 1) + rgb2gray(rgb01) + rgb2gray(rgb10));
        end
    end

    // ==========================================
    // Stage 2: Add neutral gray (128) and Clamp
    // ==========================================
    wire signed [13:0] emboss_result = emboss_sum + 13'd128; // Shift to visible range

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            emboss_gray <= 8'd128;
        end else begin
            if (emboss_result[13]) begin 
                // Negative (underflow)
                emboss_gray <= 8'd0;
            end else if (emboss_result > 255) begin
                // Positive (overflow)
                emboss_gray <= 8'd255;
            end else begin
                // Normal range
                emboss_gray <= emboss_result[7:0];
            end
        end
    end

endmodule
