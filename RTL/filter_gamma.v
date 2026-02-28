`timescale 1ns/1ps

// filter_gamma.v
// Ideal Gamma 2.2 Re-encoding using a 4096-entry LUT.
// Converts 12-bit linear values back to 8-bit non-linear sRGB for display.
//
// Pipeline delay: 1 clock (registered output)

module filter_gamma (
    input  wire        clk,
    input  wire        reset_n,

    // Control
    input  wire        gamma_en,

    // Video Stream Input (12-bit per channel)
    input  wire [35:0] din,
    input  wire        hs_in,
    input  wire        vs_in,
    input  wire        de_in,

    // Video Stream Output (8-bit per channel)
    output reg  [23:0] dout,
    output reg         hs_out,
    output reg         vs_out,
    output reg         de_out
);

    // -------------------------------------------------------------------------
    // Ideal Linear -> sRGB 8-bit LUT (4096 x 8-bit)
    // -------------------------------------------------------------------------
    reg [7:0] lut_mem [0:4095];

    initial begin
        $readmemh("gamma_lut.hex", lut_mem);
    end

    // -------------------------------------------------------------------------
    // Pipeline (1 clock)
    // -------------------------------------------------------------------------
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            dout   <= 24'd0;
            hs_out <= 1'b1;
            vs_out <= 1'b1;
            de_out <= 1'b0;
        end else begin
            hs_out <= hs_in;
            vs_out <= vs_in;
            de_out <= de_in;

            if (gamma_en) begin
                dout[23:16] <= lut_mem[din[35:24]]; // R
                dout[15:8]  <= lut_mem[din[23:12]];  // G
                dout[7:0]   <= lut_mem[din[11:0]];   // B
            end else begin
                // Bypass: downscale 12-bit to 8-bit (RShift 4)
                dout <= {din[35:28], din[23:16], din[11:4]};
            end
        end
    end

endmodule
