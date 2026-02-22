`timescale 1ns/1ps

// filter_degamma.v
// Ideal sRGB De-Gamma (Gamma 2.2 inverse) using a 256-entry LUT.
// Converts 8-bit non-linear sRGB to 12-bit linear values.
//
// Pipeline delay: 1 clock (registered output)

module filter_degamma (
    input  wire        clk,
    input  wire        reset_n,

    // Control
    input  wire        degamma_en,

    // Video Stream Input (8-bit per channel)
    input  wire [23:0] din,
    input  wire        hs_in,
    input  wire        vs_in,
    input  wire        de_in,

    // Video Stream Output (12-bit per channel)
    output reg  [35:0] dout,
    output reg         hs_out,
    output reg         vs_out,
    output reg         de_out
);

    // -------------------------------------------------------------------------
    // Ideal sRGB -> Linear 12-bit LUT (256 x 12-bit)
    // -------------------------------------------------------------------------
    reg [11:0] lut_mem [0:255];

    initial begin
        $readmemh("degamma_lut.hex", lut_mem);
    end

    // -------------------------------------------------------------------------
    // Pipeline (1 clock)
    // -------------------------------------------------------------------------
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            dout   <= 36'd0;
            hs_out <= 1'b1;
            vs_out <= 1'b1;
            de_out <= 1'b0;
        end else begin
            hs_out <= hs_in;
            vs_out <= vs_in;
            de_out <= de_in;

            if (degamma_en) begin
                dout[35:24] <= lut_mem[din[23:16]]; // R
                dout[23:12] <= lut_mem[din[15:8]];  // G
                dout[11:0]  <= lut_mem[din[7:0]];   // B
            end else begin
                // Bypass: upscale 8-bit to 12-bit (LShift 4)
                dout <= {din[23:16], 4'b0, din[15:8], 4'b0, din[7:0], 4'b0};
            end
        end
    end

endmodule
