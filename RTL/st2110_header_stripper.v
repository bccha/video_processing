`timescale 1ns / 1ps

/*
 * ==============================================================================
 * ST 2110 Header Stripper (Avalon-ST)
 * ==============================================================================
 * Description:
 * This module strips the 62-byte Ethernet+IP+UDP+RTP+SRD headers from the 
 * incoming Avalon-ST data stream (from mSGDMA) and forwards only the pure
 * 1440-byte video payload.
 * 
 * It snoops the RTP Marker bit and SRD Line/Offset fields as they pass by
 * to accurately generate `aso_startofpacket` (SOP) and `aso_endofpacket` (EOP)
 * signals for the downstream Video Pipeline, ensuring perfect VSync alignment.
 *
 * Header Map (0-indexed):
 * - Byte 0~41: MAC(14) + IP(20) + UDP(8)
 * - Byte 42~53: RTP Header (12 bytes)
 *   -> Byte 43[7]: Marker Bit (M)
 * - Byte 54~61: SRD Header (8 bytes)
 *   -> Byte 58~59: Line Number (16-bit)
 *   -> Byte 60~61: Offset (16-bit)
 * - Byte 62~1501: Video Payload (1440 bytes)
 * ==============================================================================
 */

module st2110_header_stripper #(
    parameter HEADER_BYTES  = 62,
    parameter PAYLOAD_BYTES = 1440
)(
    input  wire        clk,
    input  wire        reset_n,

    // Avalon-ST Sink (from mSGDMA reading DDR)
    input  wire [7:0]  asi_data,
    input  wire        asi_valid,
    output wire        asi_ready,
    input  wire        asi_startofpacket,
    input  wire        asi_endofpacket,

    // Avalon-ST Source (to Video Pipeline / VDMA)
    output wire [7:0]  aso_data,
    output wire        aso_valid,
    input  wire        aso_ready,
    output wire        aso_startofpacket,
    output wire        aso_endofpacket
);

    // FSM States
    localparam STATE_HEADER  = 1'b0;
    localparam STATE_PAYLOAD = 1'b1;

    reg state;
    reg [10:0] byte_count;

    // Registers to snoop header info
    reg rtp_marker;
    reg [15:0] srd_line;
    reg [15:0] srd_offset;

    // Handshake
    wire transfer_in  = asi_valid && asi_ready;
    wire transfer_out = aso_valid && aso_ready;

    // The sink is fully ready when dropping headers.
    // When forwarding payload, readiness depends on the downstream module.
    assign asi_ready = (state == STATE_HEADER) ? 1'b1 : aso_ready;

    // The source is valid only during the payload state and when input is valid.
    assign aso_valid = (state == STATE_PAYLOAD) && asi_valid;
    assign aso_data  = asi_data;

    // ---------------------------------------------------------
    // SOP & EOP Generation Logic
    // ---------------------------------------------------------
    // SOP: First byte of the payload, ONLY IF this is Line 0 and Offset 0 (Start of Frame)
    wire is_first_pixel = (srd_line == 16'd0) && (srd_offset == 16'd0) && (byte_count == 11'd0);
    assign aso_startofpacket = (state == STATE_PAYLOAD) && is_first_pixel;

    // EOP: Last byte of the payload, ONLY IF the RTP Marker bit was 1 (End of Frame)
    wire is_last_pixel = (rtp_marker == 1'b1) && (byte_count == PAYLOAD_BYTES - 1);
    assign aso_endofpacket = (state == STATE_PAYLOAD) && is_last_pixel;


    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            state       <= STATE_HEADER;
            byte_count  <= 11'd0;
            rtp_marker  <= 1'b0;
            srd_line    <= 16'd0;
            srd_offset  <= 16'd0;
        end else begin
            if (transfer_in) begin
                // Synchronize to packet boundaries precisely (Robustness against dropped bytes/starts)
                if (state == STATE_HEADER || asi_startofpacket) begin
                    // If asi_startofpacket asserts, treat this as byte 0 regardless of previous state
                    if (asi_startofpacket) begin
                        byte_count <= 11'd0;
                    end
                    
                    // Snoop necessary fields while dropping the header
                    if ((asi_startofpacket ? 11'd0 : byte_count) == 11'd43) begin
                        rtp_marker <= asi_data[7]; // M bit inside RTP header
                    end
                    else if ((asi_startofpacket ? 11'd0 : byte_count) == 11'd58) srd_line[15:8] <= asi_data;
                    else if ((asi_startofpacket ? 11'd0 : byte_count) == 11'd59) srd_line[7:0]  <= asi_data;
                    else if ((asi_startofpacket ? 11'd0 : byte_count) == 11'd60) srd_offset[15:8] <= asi_data;
                    else if ((asi_startofpacket ? 11'd0 : byte_count) == 11'd61) srd_offset[7:0]  <= asi_data;

                    // Transition to PAYLOAD
                    if ((asi_startofpacket ? 11'd0 : byte_count) == HEADER_BYTES - 1) begin
                        state      <= STATE_PAYLOAD;
                        byte_count <= 11'd0;
                    end else begin
                        state      <= STATE_HEADER; // Ensure we are in header state
                        byte_count <= (asi_startofpacket ? 11'd0 : byte_count) + 11'd1;
                    end
                end 
                else if (state == STATE_PAYLOAD) begin
                    // Quick abort if we unexpectedly get a new packet start during payload
                    if (asi_startofpacket) begin
                        state      <= STATE_HEADER;
                        byte_count <= 11'd1; // Just processed byte 0
                    end 
                    // Normal transition back to HEADER
                    else if (byte_count == PAYLOAD_BYTES - 1) begin
                        state      <= STATE_HEADER;
                        byte_count <= 11'd0;
                    end else begin
                        byte_count <= byte_count + 11'd1;
                    end
                end
            end
        end
    end

endmodule
