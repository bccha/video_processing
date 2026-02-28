`timescale 1ns/1ps

// filter_color_matrix.v
// 3x3 Color Gamut Transfer Matrix with Avalon-MM Slave Interface.
//
// Pipeline: 3 clocks (Stage1: Mul, Stage2: Add, Stage3: Shift+Clamp)
// Coefficients: 12-bit signed Q2.10 (x1024 fixed-point)
// Default: Identity matrix (C00=C11=C22=1024, off-diagonal=0)
//
// Avalon-MM Slave (clk_csr domain, word-addressed):
//   Addr  0 : Control  - bit[0] = matrix_en (0=bypass)
//   Addr  1 : C00  (Row0 Col0 : R from R)
//   Addr  2 : C01  (Row0 Col1 : R from G)
//   Addr  3 : C02  (Row0 Col2 : R from B)
//   Addr  4 : C10  (Row1 Col0 : G from R)
//   Addr  5 : C11  (Row1 Col1 : G from G)
//   Addr  6 : C12  (Row1 Col2 : G from B)
//   Addr  7 : C20  (Row2 Col0 : B from R)
//   Addr  8 : C21  (Row2 Col1 : B from G)
//   Addr  9 : C22  (Row2 Col2 : B from B)

module filter_color_matrix (
    input  wire        clk,       // Pixel clock (clk_hdmi domain)
    input  wire        clk_csr,   // CSR clock (clk_50 domain)
    input  wire        reset_n,

    // Avalon-MM Slave (clk_csr domain)
    input  wire [3:0]  s_address,
    input  wire        s_write,
    input  wire [31:0] s_writedata,
    input  wire        s_read,
    output reg  [31:0] s_readdata,

    // Video Stream (clk domain)
    input  wire [35:0] din,
    input  wire        hs_in,
    input  wire        vs_in,
    input  wire        de_in,

    output reg  [35:0] dout,
    output reg         hs_out,
    output reg         vs_out,
    output reg         de_out,
    output wire        matrix_en_out  // synced to clk domain, for degamma/gamma auto-enable
);

    // =========================================================================
    // CSR Registers (clk_csr domain)
    // =========================================================================
    reg        matrix_en_csr;
    reg signed [11:0] c00_csr, c01_csr, c02_csr;
    reg signed [11:0] c10_csr, c11_csr, c12_csr;
    reg signed [11:0] c20_csr, c21_csr, c22_csr;

    // Initialize to Identity (x1024)
    initial begin
        matrix_en_csr = 1'b0;
        c00_csr = 12'sd1024; c01_csr = 12'sd0;    c02_csr = 12'sd0;
        c10_csr = 12'sd0;    c11_csr = 12'sd1024; c12_csr = 12'sd0;
        c20_csr = 12'sd0;    c21_csr = 12'sd0;    c22_csr = 12'sd1024;
    end

    // Avalon-MM Write
    always @(posedge clk_csr or negedge reset_n) begin
        if (!reset_n) begin
            matrix_en_csr <= 1'b0;
            c00_csr <= 12'sd1024; c01_csr <= 12'sd0;    c02_csr <= 12'sd0;
            c10_csr <= 12'sd0;    c11_csr <= 12'sd1024; c12_csr <= 12'sd0;
            c20_csr <= 12'sd0;    c21_csr <= 12'sd0;    c22_csr <= 12'sd1024;
        end else begin
            if (s_write) begin
                case (s_address)
                    4'd0: matrix_en_csr   <= s_writedata[0];
                    4'd1: c00_csr         <= s_writedata[11:0];
                    4'd2: c01_csr         <= s_writedata[11:0];
                    4'd3: c02_csr         <= s_writedata[11:0];
                    4'd4: c10_csr         <= s_writedata[11:0];
                    4'd5: c11_csr         <= s_writedata[11:0];
                    4'd6: c12_csr         <= s_writedata[11:0];
                    4'd7: c20_csr         <= s_writedata[11:0];
                    4'd8: c21_csr         <= s_writedata[11:0];
                    4'd9: c22_csr         <= s_writedata[11:0];
                    default: ;
                endcase
            end
        end
    end

    // Avalon-MM Read
    always @(posedge clk_csr or negedge reset_n) begin
        if (!reset_n) begin
            s_readdata <= 32'd0;
        end else if (s_read) begin
            case (s_address)
                4'd0: s_readdata <= {31'd0, matrix_en_csr};
                4'd1: s_readdata <= {{20{c00_csr[11]}}, c00_csr};
                4'd2: s_readdata <= {{20{c01_csr[11]}}, c01_csr};
                4'd3: s_readdata <= {{20{c02_csr[11]}}, c02_csr};
                4'd4: s_readdata <= {{20{c10_csr[11]}}, c10_csr};
                4'd5: s_readdata <= {{20{c11_csr[11]}}, c11_csr};
                4'd6: s_readdata <= {{20{c12_csr[11]}}, c12_csr};
                4'd7: s_readdata <= {{20{c20_csr[11]}}, c20_csr};
                4'd8: s_readdata <= {{20{c21_csr[11]}}, c21_csr};
                4'd9: s_readdata <= {{20{c22_csr[11]}}, c22_csr};
                default: s_readdata <= 32'd0;
            endcase
        end
    end

    // =========================================================================
    // CDC: Sync coefficients to pixel clock domain (2-stage FF)
    // =========================================================================
    reg        matrix_en_s1, matrix_en_s2;
    reg signed [11:0] c00_s1, c00_s2;
    reg signed [11:0] c01_s1, c01_s2;
    reg signed [11:0] c02_s1, c02_s2;
    reg signed [11:0] c10_s1, c10_s2;
    reg signed [11:0] c11_s1, c11_s2;
    reg signed [11:0] c12_s1, c12_s2;
    reg signed [11:0] c20_s1, c20_s2;
    reg signed [11:0] c21_s1, c21_s2;
    reg signed [11:0] c22_s1, c22_s2;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            matrix_en_s1 <= 0; matrix_en_s2 <= 0;
            c00_s1 <= 12'sd1024; c00_s2 <= 12'sd1024;
            c01_s1 <= 12'sd0;    c01_s2 <= 12'sd0;
            c02_s1 <= 12'sd0;    c02_s2 <= 12'sd0;
            c10_s1 <= 12'sd0;    c10_s2 <= 12'sd0;
            c11_s1 <= 12'sd1024; c11_s2 <= 12'sd1024;
            c12_s1 <= 12'sd0;    c12_s2 <= 12'sd0;
            c20_s1 <= 12'sd0;    c20_s2 <= 12'sd0;
            c21_s1 <= 12'sd0;    c21_s2 <= 12'sd0;
            c22_s1 <= 12'sd1024; c22_s2 <= 12'sd1024;
        end else begin
            matrix_en_s1 <= matrix_en_csr;
            c00_s1 <= c00_csr; c01_s1 <= c01_csr; c02_s1 <= c02_csr;
            c10_s1 <= c10_csr; c11_s1 <= c11_csr; c12_s1 <= c12_csr;
            c20_s1 <= c20_csr; c21_s1 <= c21_csr; c22_s1 <= c22_csr;
            matrix_en_s2 <= matrix_en_s1;
            c00_s2 <= c00_s1; c01_s2 <= c01_s1; c02_s2 <= c02_s1;
            c10_s2 <= c10_s1; c11_s2 <= c11_s1; c12_s2 <= c12_s1;
            c20_s2 <= c20_s1; c21_s2 <= c21_s1; c22_s2 <= c22_s1;
        end
    end

    assign matrix_en_out = matrix_en_s2;

    // =========================================================================
    // Stage 1: Multiply  (12-bit unsigned * 12-bit signed = 24-bit signed)
    // =========================================================================
    reg signed [23:0] mul_r0, mul_r1, mul_r2; 
    reg signed [23:0] mul_g0, mul_g1, mul_g2; 
    reg signed [23:0] mul_b0, mul_b1, mul_b2; 

    reg [35:0] din_st1;
    reg        hs_st1, vs_st1, de_st1, en_st1;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            mul_r0 <= 0; mul_r1 <= 0; mul_r2 <= 0;
            mul_g0 <= 0; mul_g1 <= 0; mul_g2 <= 0;
            mul_b0 <= 0; mul_b1 <= 0; mul_b2 <= 0;
            hs_st1 <= 1; vs_st1 <= 1; de_st1 <= 0; en_st1 <= 0;
            din_st1 <= 0;
        end else begin
            // Stage 1: Multiplications
            mul_r0 <= $signed({1'b0, din[35:24]}) * c00_s2;
            mul_r1 <= $signed({1'b0, din[23:12]}) * c01_s2;
            mul_r2 <= $signed({1'b0, din[11:0]})  * c02_s2;

            mul_g0 <= $signed({1'b0, din[35:24]}) * c10_s2;
            mul_g1 <= $signed({1'b0, din[23:12]}) * c11_s2;
            mul_g2 <= $signed({1'b0, din[11:0]})  * c12_s2;

            mul_b0 <= $signed({1'b0, din[35:24]}) * c20_s2;
            mul_b1 <= $signed({1'b0, din[23:12]}) * c21_s2;
            mul_b2 <= $signed({1'b0, din[11:0]})  * c22_s2;

            hs_st1  <= hs_in;
            vs_st1  <= vs_in;
            de_st1  <= de_in;
            en_st1  <= matrix_en_s2;
            din_st1 <= din;
        end
    end

    // =========================================================================
    // Stage 2: Adder Tree  (24-bit + 24-bit + 24-bit = 26-bit signed)
    // =========================================================================
    reg signed [25:0] sum_r, sum_g, sum_b;

    reg [35:0] din_st2;
    reg        hs_st2, vs_st2, de_st2, en_st2;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            sum_r <= 0; sum_g <= 0; sum_b <= 0;
            hs_st2 <= 1; vs_st2 <= 1; de_st2 <= 0; en_st2 <= 0;
            din_st2 <= 0;
        end else begin
            sum_r <= mul_r0 + mul_r1 + mul_r2;
            sum_g <= mul_g0 + mul_g1 + mul_g2;
            sum_b <= mul_b0 + mul_b1 + mul_b2;

            hs_st2  <= hs_st1;
            vs_st2  <= vs_st1;
            de_st2  <= de_st1;
            en_st2  <= en_st1;
            din_st2 <= din_st1;
        end
    end

    // =========================================================================
    // Stage 3: Arithmetic Right Shift (>>>10) + Clamp to [0, 4095] (12-bit)
    // =========================================================================
    wire signed [15:0] final_r = sum_r >>> 10;
    wire signed [15:0] final_g = sum_g >>> 10;
    wire signed [15:0] final_b = sum_b >>> 10;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            dout   <= 36'd0;
            hs_out <= 1'b1;
            vs_out <= 1'b1;
            de_out <= 1'b0;
        end else begin
            hs_out <= hs_st2;
            vs_out <= vs_st2;
            de_out <= de_st2;

            if (en_st2) begin
                // Red
                if      (final_r[15])         dout[35:24] <= 12'd0;
                else if (final_r > 16'sd4095) dout[35:24] <= 12'd4095;
                else                          dout[35:24] <= final_r[11:0];
                // Green
                if      (final_g[15])         dout[23:12] <= 12'd0;
                else if (final_g > 16'sd4095) dout[23:12] <= 12'd4095;
                else                          dout[23:12] <= final_g[11:0];
                // Blue
                if      (final_b[15])         dout[11:0]  <= 12'd0;
                else if (final_b > 16'sd4095) dout[11:0]  <= 12'd4095;
                else                          dout[11:0]  <= final_b[11:0];
            end else begin
                // Bypass: pass-through (12-bit)
                dout <= din_st2;
            end
        end
    end

endmodule
