`timescale 1ns/1ps

module video_dma_master (
    input  wire         clk,
    input  wire         reset_n,
    input  wire [31:0]  start_addr,
    
    // Control & Status
    input  wire         dma_start,   // Pulse to start a single frame transfer
    input  wire         dma_cont_en, // Continuous mode enable
    output reg          dma_done,    // Pulse when one frame is finished
    output wire         busy,
    input  wire         vsync_edge,  // Trigger for new frame in continuous mode
    
    // Avalon-MM Master Interface
    input  wire         m_waitrequest,
    input  wire [31:0]  m_readdata,
    input  wire         m_readdatavalid,
    output reg  [31:0]  m_address,
    output reg          m_read,
    output wire [7:0]   m_burstcount,
    
    // FIFO Interface (Write side)
    input  wire [8:0]   fifo_used,
    output wire         fifo_wr_en,
    output wire [31:0]  fifo_wr_data
);

    // Initial Parameters
    parameter BURST_LEN = 8'd64;       // Burst size (256 bytes)
    parameter FIFO_DEPTH = 512;        // FIFO size in words
    parameter H_RES = 1280;
    parameter V_RES = 720;
    parameter FRAME_SIZE_WORDS = H_RES * V_RES; // Total 32-bit words per frame

    // FSM States
    localparam IDLE      = 2'b00;
    localparam CHECK_FIFO= 2'b01; // Check if we can issue a read command
    localparam ISSUE_READ= 2'b10; // Issue Avalon Read Command
    localparam WAIT_END  = 2'b11; // Wait for all data to return

    reg [1:0] state;
    reg [31:0] current_read_addr;
    
    // Counters for Flow Control
    reg [31:0] words_commanded; // Total words requested so far
    reg [31:0] words_received;  // Total words received so far from Avalon
    reg [9:0]  pending_bursts;  // Number of bursts issued but not fully received
    
    reg is_cont_mode;
    reg frame_active; // Starts on Trigger, Ends when words_received == FRAME_SIZE

    // Assignments
    assign m_burstcount = BURST_LEN;
    assign fifo_wr_en   = m_readdatavalid;
    assign fifo_wr_data = m_readdata;
    assign busy         = frame_active;

    // ------------------------------------------------------------------
    // 1. Main Control FSM (Command Issuer)
    // ------------------------------------------------------------------
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            state <= IDLE;
            m_address <= 32'd0;
            m_read <= 1'b0;
            current_read_addr <= 32'd0;
            words_commanded <= 32'd0;
            is_cont_mode <= 1'b0;
            frame_active <= 1'b0;
            pending_bursts <= 10'd0;
        end else begin
            // Default signals
            // Default signals
            // dma_done is driven by separate logic
            
            // Pending Bursts Counter
            // Increment on Command Issue success
            // Decrement on Burst Completion? No, decrement on every word received is hard.
            // Let's track pending words instead?
            // Simpler: pending_words = words_commanded - words_received.
            
            case (state)
                IDLE: begin
                    m_read <= 1'b0;
                    words_commanded <= 32'd0;
                    
                    // Trigger Logic
                    if (dma_start) begin
                        current_read_addr <= start_addr;
                        is_cont_mode <= 1'b0;
                        frame_active <= 1'b1;
                        state <= CHECK_FIFO;
                    end else if (dma_cont_en && vsync_edge) begin
                        current_read_addr <= start_addr;
                        is_cont_mode <= 1'b1;
                        frame_active <= 1'b1;
                        state <= CHECK_FIFO;
                    end else begin
                        frame_active <= 1'b0;
                    end
                end

                CHECK_FIFO: begin
                    m_read <= 1'b0;
                    
                    // 1. Check if we have issued all commands for this frame
                    if (words_commanded >= FRAME_SIZE_WORDS) begin
                        state <= WAIT_END;
                    end
                    // 2. Check FIFO Overflow Risk
                    // Condition: (Used + Pending_from_commands) < (Depth - Command_Size)
                    // pending_words = words_commanded - words_received (Calculated below)
                    // If FIFO has space for at least one more burst:
                    else if ((fifo_used + (words_commanded - words_received)) <= (FIFO_DEPTH - BURST_LEN - 2)) begin
                        // Safe to issue a read
                        m_address <= current_read_addr;
                        m_read <= 1'b1;
                        state <= ISSUE_READ;
                    end
                    // Else: Wait here until data is drained from FIFO or received
                end

                ISSUE_READ: begin
                    if (!m_waitrequest) begin
                        // Command Accepted
                        m_read <= 1'b0;
                        current_read_addr <= current_read_addr + (BURST_LEN * 4);
                        words_commanded <= words_commanded + BURST_LEN;
                        state <= CHECK_FIFO;
                    end
                    // Else: Stay in ISSUE_READ with m_read high
                end

                WAIT_END: begin
                    m_read <= 1'b0;
                    // Wait for Data Receiver to catch up
                    if (words_received >= FRAME_SIZE_WORDS) begin
                        state <= IDLE;
                        // frame_active will be cleared by data logic or here?
                        // Let's clear it here.
                        frame_active <= 1'b0;
                    end
                end
            endcase
            
            // Emergency Stop (Only in Continuous Mode, between frames or forceful?)
            if (is_cont_mode && !dma_cont_en && state == IDLE) begin
                is_cont_mode <= 1'b0;
            end
        end
    end

    // ------------------------------------------------------------------
    // 2. Data Receiver and Done Logic
    // ------------------------------------------------------------------
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            words_received <= 32'd0;
        end else begin
            // Reset received count when starting a new frame
            if (state == IDLE && (dma_start || (dma_cont_en && vsync_edge))) begin
                words_received <= 32'd0;
            end
            
            // Count valid data
            if (m_readdatavalid) begin
                words_received <= words_received + 1;
            end
        end
    end

    // ------------------------------------------------------------------
    // 3. Done Pulse Generation
    // ------------------------------------------------------------------
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) dma_done <= 1'b0;
        else begin
            // Fire Done when we just finished receiving the last word
            if (m_readdatavalid && (words_received == FRAME_SIZE_WORDS - 1)) begin
                dma_done <= 1'b1;
            end else begin
                dma_done <= 1'b0;
            end
        end
    end

endmodule
