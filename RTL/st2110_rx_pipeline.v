`timescale 1ns / 1ps

module st2110_rx_pipeline (
    input  wire        clk,
    input  wire        reset_n,

    // Avalon-ST Sink (from Qsys rx_dma_read)
    input  wire [7:0]  rx_dma_read_data,
    input  wire        rx_dma_read_valid,
    output wire        rx_dma_read_ready,
    input  wire        rx_dma_read_startofpacket,
    input  wire        rx_dma_read_endofpacket,

    // Avalon-ST Source (to Qsys rx_dma_write)
    output wire [31:0] rx_dma_write_data,
    output wire        rx_dma_write_valid,
    input  wire        rx_dma_write_ready,
    output wire        rx_dma_write_startofpacket,
    output wire        rx_dma_write_endofpacket,
    output wire [1:0]  rx_dma_write_empty
);

    // Internal signals between Stripper and Alignment Wrapper
    wire [7:0] aligner_asi_data;
    wire       aligner_asi_valid;
    wire       aligner_asi_ready;
    wire       aligner_asi_startofpacket;
    wire       aligner_asi_endofpacket;

    // We always output 32-bit aligned pixels (RGB + dummy/alpha), so empty is always 0
    assign rx_dma_write_empty = 2'b00;

    // ST2110 Pipeline: DMA Read -> Header Stripper -> Alignment Wrapper -> DMA Write
    st2110_header_stripper u_header_stripper (
        .clk               (clk),
        .reset_n           (reset_n),
        // from Qsys DMA Read (8-bit)
        .asi_data          (rx_dma_read_data),
        .asi_valid         (rx_dma_read_valid),
        .asi_ready         (rx_dma_read_ready),
        .asi_startofpacket (rx_dma_read_startofpacket),
        .asi_endofpacket   (rx_dma_read_endofpacket),
        // to aligner
        .aso_data          (aligner_asi_data),
        .aso_valid         (aligner_asi_valid),
        .aso_ready         (aligner_asi_ready),
        .aso_startofpacket (aligner_asi_startofpacket),
        .aso_endofpacket   (aligner_asi_endofpacket)
    );

    st2110_alignment_wrapper u_alignment_wrapper (
        .clk               (clk),
        .reset_n           (reset_n),
        // from stripper
        .asi_data          (aligner_asi_data),
        .asi_valid         (aligner_asi_valid),
        .asi_ready         (aligner_asi_ready),
        .asi_startofpacket (aligner_asi_startofpacket),
        .asi_endofpacket   (aligner_asi_endofpacket),
        // to Qsys DMA Write (32-bit)
        .aso_data          (rx_dma_write_data),
        .aso_valid         (rx_dma_write_valid),
        .aso_ready         (rx_dma_write_ready),
        .aso_startofpacket (rx_dma_write_startofpacket),
        .aso_endofpacket   (rx_dma_write_endofpacket)
    );

endmodule
