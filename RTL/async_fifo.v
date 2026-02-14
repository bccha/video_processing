`timescale 1ns/1ps

module async_fifo #(
    parameter DATA_WIDTH = 32,
    parameter ADDR_WIDTH = 9
)(
    input  wire                   wr_clk,
    input  wire                   wr_rst_n,
    input  wire                   wr_en,
    input  wire [DATA_WIDTH-1:0]  wr_data,
    output wire [ADDR_WIDTH-1:0]  wr_used,

    input  wire                   rd_clk,
    input  wire                   rd_rst_n,
    input  wire                   rd_en,
    output wire [DATA_WIDTH-1:0]  rd_data,
    output wire                   rd_empty
);
    // Behavioral Model for Simulation
    localparam DEPTH = 1 << ADDR_WIDTH;
    reg [DATA_WIDTH-1:0] mem [0:DEPTH-1];
    reg [ADDR_WIDTH:0] wr_ptr, rd_ptr;
    
    // Simplistic CDC for simulation (not for synthesis!)
    always @(posedge wr_clk or negedge wr_rst_n) begin
        if (!wr_rst_n) begin
            wr_ptr <= 0;
        end else if (wr_en && (wr_ptr - rd_ptr < DEPTH)) begin
            mem[wr_ptr[ADDR_WIDTH-1:0]] <= wr_data;
            wr_ptr <= wr_ptr + 1;
        end
    end

    always @(posedge rd_clk or negedge rd_rst_n) begin
        if (!rd_rst_n) begin
            rd_ptr <= 0;
        end else if (rd_en && (wr_ptr != rd_ptr)) begin
            rd_ptr <= rd_ptr + 1;
        end
    end

    assign rd_data = mem[rd_ptr[ADDR_WIDTH-1:0]];
    assign rd_empty = (wr_ptr == rd_ptr);
    assign wr_used = wr_ptr - rd_ptr;

endmodule
