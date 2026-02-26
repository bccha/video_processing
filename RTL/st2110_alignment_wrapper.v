`timescale 1ns / 1ps

/*
 * ==============================================================================
 * ST 2110 Data Alignment Wrapper (8-bit to 32-bit Avalon-ST)
 * ==============================================================================
 * Description:
 * This module takes the 8-bit pure video payload from the `st2110_header_stripper`
 * and packs it into 32-bit words (RGB888 + 8-bit dummy/alpha) required by the 
 * downstream Video DMA (Clocked Video Output pipeline).
 *
 * ST 2110-20 RGB 24-bit unpacking logic:
 * - 1st Byte (asi_data) -> Red   (aso_data[23:16])
 * - 2nd Byte (asi_data) -> Green (aso_data[15:8])
 * - 3rd Byte (asi_data) -> Blue  (aso_data[7:0])
 * - 4th Byte (Logic 0)  -> Dummy (aso_data[31:24]) (For 32-bit memory alignment)
 *
 * It carefully preserves the SOP (Start of Packet) and EOP (End of Packet)
 * signals, asserting them on the correct 32-bit word boundary.
 * ==============================================================================
 */

module st2110_alignment_wrapper (
    input  wire        clk,
    input  wire        reset_n,

    // Avalon-ST Sink (8-bit, from Header Stripper)
    input  wire [7:0]  asi_data,
    input  wire        asi_valid,
    output wire        asi_ready,
    input  wire        asi_startofpacket,
    input  wire        asi_endofpacket,

    // Avalon-ST Source (32-bit, to Video DMA / Pipeline)
    output wire [31:0] aso_data,
    output wire        aso_valid,
    input  wire        aso_ready,
    output wire        aso_startofpacket,
    output wire        aso_endofpacket
);

    // 2-bit counter to track byte position within a 3-byte pixel (RGB)
    reg [1:0] byte_cnt;
    
    // Shift register to accumulate 3 bytes (24 bits) into a 32-bit word
    reg [31:0] shift_reg;
    
    // Output registers for Avalon-ST Source
    reg [31:0] out_data;
    reg        out_valid;
    reg        out_sop;
    reg        out_eop;
    
    // Register to delay SOP until a full 32-bit pixel is assembled
    reg        is_sop_pending;

    // We only accept new bytes when the downstream is ready OR our output register is empty
    wire sink_ready = aso_ready || !out_valid;
    assign asi_ready = sink_ready;

    // Connect outputs
    assign aso_data          = out_data;
    assign aso_valid         = out_valid;
    assign aso_startofpacket = out_sop;
    assign aso_endofpacket   = out_eop;

    // Handshake
    wire transfer_in  = asi_valid && asi_ready;
    wire transfer_out = aso_valid && aso_ready;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            byte_cnt  <= 2'd0;
            shift_reg <= 32'd0;
            out_valid <= 1'b0;
            out_sop   <= 1'b0;
            out_eop   <= 1'b0;
            out_data  <= 32'd0;
        end else begin
            // 1. Handle downstream consuming our output
            if (transfer_out) begin
                out_valid <= 1'b0; // Output consumed, register is empty
                out_sop   <= 1'b0;
                out_eop   <= 1'b0;
            end

            // 2. Handle incoming 8-bit bytes
            if (transfer_in) begin
                // Capture SOP
                if (asi_startofpacket) begin
                    byte_cnt <= 2'd0;
                    shift_reg[31:24] <= 8'd0;
                    shift_reg[23:16] <= asi_data; // R
                    byte_cnt <= 2'd1;
                    is_sop_pending <= 1'b1;       // Remember SOP for the upcoming valid pixel
                end else begin
                    case (byte_cnt)
                        2'd0: begin
                            shift_reg[31:24] <= 8'd0;         
                            shift_reg[23:16] <= asi_data;     // R
                            byte_cnt <= 2'd1;
                        end
                        2'd1: begin
                            shift_reg[15:8]  <= asi_data;     // G
                            byte_cnt <= 2'd2;
                        end
                        2'd2: begin
                            shift_reg[7:0]   <= asi_data;     // B
                            byte_cnt <= 2'd0;
                            
                            // We have accumulated a full 3-byte pixel.
                            // The output is 32-bit: {8'd0, R, G, B}
                            out_data  <= {8'd0, shift_reg[23:8], asi_data};
                            out_valid <= 1'b1;
                            
                            // Emit SOP if this pixel was the first in the packet
                            out_sop   <= is_sop_pending;
                            is_sop_pending <= 1'b0;
                            
                            // Emit EOP if this byte is the last in the packet
                            out_eop   <= asi_endofpacket;
                        end
                    endcase
                end
            end
        end
    end

endmodule
