`timescale 1ns/1ps

module simple_dcfifo #(
    parameter DATA_WIDTH = 32,
    parameter ADDR_WIDTH = 9  // 512 depth
)(
    input  wire                  wrclk,
    input  wire [DATA_WIDTH-1:0] data,
    input  wire                  wrreq,
    output wire [ADDR_WIDTH-1:0] wrusedw,
    output wire                  wrfull,
    
    input  wire                  rdclk,
    input  wire                  rdreq,
    output reg  [DATA_WIDTH-1:0] q,
    output wire                  rdempty
);

    // ----------------------------------------------------------------
    // 1. Pointers & CDC
    // ----------------------------------------------------------------
    // Pointers are ADDR_WIDTH+1 bits to distinguish Full/Empty
    reg [ADDR_WIDTH:0] wr_ptr_bin;
    reg [ADDR_WIDTH:0] wr_ptr_gray;
    reg [ADDR_WIDTH:0] rd_ptr_bin;
    reg [ADDR_WIDTH:0] rd_ptr_gray;

    reg [ADDR_WIDTH:0] wr_ptr_gray_sync1, wr_ptr_gray_sync2;
    reg [ADDR_WIDTH:0] rd_ptr_gray_sync1, rd_ptr_gray_sync2;

    // ----------------------------------------------------------------
    // 2. Memory (Infer Block RAM)
    // ----------------------------------------------------------------
    reg [DATA_WIDTH-1:0] mem [(1<<ADDR_WIDTH)-1:0];

    // ----------------------------------------------------------------
    // 3. Synchronization (Double Flop)
    // ----------------------------------------------------------------
    always @(posedge wrclk) begin
        rd_ptr_gray_sync1 <= rd_ptr_gray;
        rd_ptr_gray_sync2 <= rd_ptr_gray_sync1;
    end

    always @(posedge rdclk) begin
        wr_ptr_gray_sync1 <= wr_ptr_gray;
        wr_ptr_gray_sync2 <= wr_ptr_gray_sync1;
    end

    // ----------------------------------------------------------------
    // 4. Binary/Gray Functions
    // ----------------------------------------------------------------
    function [ADDR_WIDTH:0] bin2gray;
        input [ADDR_WIDTH:0] bin;
        begin
            bin2gray = bin ^ (bin >> 1);
        end
    endfunction

    function [ADDR_WIDTH:0] gray2bin;
        input [ADDR_WIDTH:0] gray;
        integer i;
        begin
            gray2bin[ADDR_WIDTH] = gray[ADDR_WIDTH];
            for (i = ADDR_WIDTH-1; i >= 0; i = i - 1)
                gray2bin[i] = gray2bin[i+1] ^ gray[i];
        end
    endfunction

    // ----------------------------------------------------------------
    // 5. Initial for Simulation
    // ----------------------------------------------------------------
    integer i;
    initial begin
        wr_ptr_bin = 0; wr_ptr_gray = 0;
        rd_ptr_bin = 0; rd_ptr_gray = 0;
        wr_ptr_gray_sync1 = 0; wr_ptr_gray_sync2 = 0;
        rd_ptr_gray_sync1 = 0; rd_ptr_gray_sync2 = 0;
        q = 0; // Initialize output to 0 to avoid X
    end

    // ----------------------------------------------------------------
    // 6. Write Logic & Usage Calculation
    // ----------------------------------------------------------------
    wire [ADDR_WIDTH:0] rd_ptr_bin_sync = gray2bin(rd_ptr_gray_sync2);
    
    // Check Full: Gray code comparison
    // Full if top 2 bits differ, rest match
    assign wrfull = (wr_ptr_gray == {~rd_ptr_gray_sync2[ADDR_WIDTH:ADDR_WIDTH-1], rd_ptr_gray_sync2[ADDR_WIDTH-2:0]});

    // Used Words: Subtract Binary Pointers
    wire [ADDR_WIDTH:0] used_diff = wr_ptr_bin - rd_ptr_bin_sync;
    
    // Saturate to Max Value (all 1s) if actual usage is Full (bit ADDR_WIDTH is 1)
    // This protects against wrapping to 0 which would confuse the DMA Master
    assign wrusedw = (used_diff[ADDR_WIDTH]) ? {ADDR_WIDTH{1'b1}} : used_diff[ADDR_WIDTH-1:0];

    always @(posedge wrclk) begin
        if (wrreq && !wrfull) begin
            mem[wr_ptr_bin[ADDR_WIDTH-1:0]] <= data;
            wr_ptr_bin <= wr_ptr_bin + 1;
            wr_ptr_gray <= bin2gray(wr_ptr_bin + 1);
        end
    end

    // ----------------------------------------------------------------
    // 7. Read Logic
    // ----------------------------------------------------------------
    // Check Empty: Gray code pointers match exactly
    assign rdempty = (rd_ptr_gray == wr_ptr_gray_sync2);

    always @(posedge rdclk) begin
        if (rdreq && !rdempty) begin
            q <= mem[rd_ptr_bin[ADDR_WIDTH-1:0]];
            rd_ptr_bin <= rd_ptr_bin + 1;
            rd_ptr_gray <= bin2gray(rd_ptr_bin + 1);
        end
    end

endmodule
