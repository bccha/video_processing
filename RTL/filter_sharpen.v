module filter_sharpen #(
    parameter DATA_WIDTH = 24
)(
    input  wire clk,
    input  wire reset_n,
    
    // 3x3 Window (RGB)
    input  wire [DATA_WIDTH-1:0] rgb00, rgb01, rgb02,
    input  wire [DATA_WIDTH-1:0] rgb10, rgb11, rgb12,
    input  wire [DATA_WIDTH-1:0] rgb20, rgb21, rgb22,
    
    // Output (Delayed by 2 clocks to match pipeline)
    output reg  [7:0] sharp_r,
    output reg  [7:0] sharp_g,
    output reg  [7:0] sharp_b
);

    // ==========================================
    // Stage 1: Sharpen Mask Calculation
    // Sharpen Mask:
    // [  0 -1  0 ]
    // [ -1  5 -1 ]
    // [  0 -1  0 ]
    // ==========================================
    reg signed [12:0] sum_r, sum_g, sum_b;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            sum_r <= 0; sum_g <= 0; sum_b <= 0;
        end else begin
            // 5 * Center - (Top + Bottom + Left + Right)
            // Can be written as: 5*rgb11 - (rgb01 + rgb10 + rgb12 + rgb21)
            // Using bit shifts for multiplication: x*5 = (x<<2) + x
            
            // Red
            sum_r <= ({2'd0, rgb11[23:16]} + {rgb11[23:16], 2'd0}) - 
                     ({3'd0, rgb01[23:16]} + {3'd0, rgb10[23:16]} + {3'd0, rgb12[23:16]} + {3'd0, rgb21[23:16]});
            
            // Green
            sum_g <= ({2'd0, rgb11[15:8]} + {rgb11[15:8], 2'd0}) - 
                     ({3'd0, rgb01[15:8]} + {3'd0, rgb10[15:8]} + {3'd0, rgb12[15:8]} + {3'd0, rgb21[15:8]});
            
            // Blue
            sum_b <= ({2'd0, rgb11[7:0]} + {rgb11[7:0], 2'd0}) - 
                     ({3'd0, rgb01[7:0]} + {3'd0, rgb10[7:0]} + {3'd0, rgb12[7:0]} + {3'd0, rgb21[7:0]});
        end
    end

    // ==========================================
    // Stage 2: Clamping
    // ==========================================
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            sharp_r <= 0; sharp_g <= 0; sharp_b <= 0;
        end else begin
            // Red Clamp
            if (sum_r[12]) sharp_r <= 8'd0; // Negative
            else if (sum_r > 255) sharp_r <= 8'd255; // Overflow
            else sharp_r <= sum_r[7:0]; // Normal
            
            // Green Clamp
            if (sum_g[12]) sharp_g <= 8'd0;
            else if (sum_g > 255) sharp_g <= 8'd255;
            else sharp_g <= sum_g[7:0];
            
            // Blue Clamp
            if (sum_b[12]) sharp_b <= 8'd0;
            else if (sum_b > 255) sharp_b <= 8'd255;
            else sharp_b <= sum_b[7:0];
        end
    end

endmodule
