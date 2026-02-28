module filter_blur #(
    parameter DATA_WIDTH = 24
)(
    input  wire clk,
    input  wire reset_n,
    
    // 3x3 Window (RGB)
    input  wire [DATA_WIDTH-1:0] rgb00, rgb01, rgb02,
    input  wire [DATA_WIDTH-1:0] rgb10, rgb11, rgb12,
    input  wire [DATA_WIDTH-1:0] rgb20, rgb21, rgb22,
    
    // Outputs (Delayed by 2 clocks)
    output reg  [7:0] blur_r,
    output reg  [7:0] blur_g,
    output reg  [7:0] blur_b,
    output wire [7:0] blur_gray
);

    // ==========================================
    // Stage 1: Sum components
    // ==========================================
    reg [10:0] sum_r1, sum_r2, sum_r3;
    reg [10:0] sum_g1, sum_g2, sum_g3;
    reg [10:0] sum_b1, sum_b2, sum_b3;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            sum_r1 <= 0; sum_r2 <= 0; sum_r3 <= 0;
            sum_g1 <= 0; sum_g2 <= 0; sum_g3 <= 0;
            sum_b1 <= 0; sum_b2 <= 0; sum_b3 <= 0;
        end else begin
            sum_r1 <= rgb00[23:16] + rgb01[23:16] + rgb02[23:16];
            sum_r2 <= rgb10[23:16] + rgb11[23:16] + rgb12[23:16];
            sum_r3 <= rgb20[23:16] + rgb21[23:16] + rgb22[23:16];
            
            sum_g1 <= rgb00[15:8] + rgb01[15:8] + rgb02[15:8];
            sum_g2 <= rgb10[15:8] + rgb11[15:8] + rgb12[15:8];
            sum_g3 <= rgb20[15:8] + rgb21[15:8] + rgb22[15:8];
            
            sum_b1 <= rgb00[7:0] + rgb01[7:0] + rgb02[7:0];
            sum_b2 <= rgb10[7:0] + rgb11[7:0] + rgb12[7:0];
            sum_b3 <= rgb20[7:0] + rgb21[7:0] + rgb22[7:0];
        end
    end

    // ==========================================
    // Stage 2: Average Calculation (approx / 9)
    // ==========================================
    wire [11:0] tot_r = sum_r1 + sum_r2 + sum_r3;
    wire [11:0] tot_g = sum_g1 + sum_g2 + sum_g3;
    wire [11:0] tot_b = sum_b1 + sum_b2 + sum_b3;
    wire [11:0] div_r = (tot_r * 28) >> 8;
    wire [11:0] div_g = (tot_g * 28) >> 8;
    wire [11:0] div_b = (tot_b * 28) >> 8;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            blur_r <= 0; 
            blur_g <= 0; 
            blur_b <= 0;
        end else begin
            blur_r <= (div_r > 255) ? 8'd255 : div_r[7:0];
            blur_g <= (div_g > 255) ? 8'd255 : div_g[7:0];
            blur_b <= (div_b > 255) ? 8'd255 : div_b[7:0];
        end
    end

    // Grayscale conversion of the blurred pixel
    assign blur_gray = (blur_r >> 2) + (blur_g >> 1) + (blur_b >> 2);

endmodule
