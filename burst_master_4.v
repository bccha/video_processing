`timescale 1ns/1ps

/*
 * 모듈명: burst_master_4 (Handshake Pipeline Version)
 * 
 * [개요]
 * burst_master_3의 Two-FIFO 구조에 "Valid-Ready Handshake" 기반의 파이프라인을 적용했습니다.
 * 
 * [핵심 기능: Valid-Ready Handshake]
 * 각 파이프라인 스테이지는 다음 스테이지가 준비되었거나(Ready), 자신이 비어있을 때만
 * 이전 스테이지의 데이터를 받아들입니다.
 * 
 * 공식: ready[i] = !valid[i] || ready[i+1];
 * 
 * 이를 통해 파이프라인의 어느 한 곳이라도 막히면(Back Pressure), 
 * 그 신호가 맨 앞단(Input FIFO)까지 즉시 전파되어 데이터 유입을 중단합니다.
 * 
 * [Pipeline Structure]
 * Input FIFO -> [Stage 0] -> [Stage 1] -> [Stage 2] -> [Stage 3] -> Output FIFO
 * 
 * - Latency: 4 Cycles
 * - Throughput: 1 Data per Clock (if no back pressure)
 */

module burst_master_4 #(
    parameter DATA_WIDTH = 32,
    parameter ADDR_WIDTH = 32,
    parameter BURST_COUNT = 256,
    parameter FIFO_DEPTH = 512,
    parameter PIPE_LATENCY = 4
)(
    input  wire                   clk,
    input  wire                   reset_n,

    // CSR Interface
    input  wire                   avs_write,
    input  wire                   avs_read,
    input  wire [2:0]             avs_address,
    input  wire [31:0]            avs_writedata,
    output reg  [31:0]            avs_readdata,

    // Read Master
    output reg  [ADDR_WIDTH-1:0]  rm_address,
    output reg                    rm_read,
    input  wire [DATA_WIDTH-1:0]  rm_readdata,
    input  wire                   rm_readdatavalid,
    output reg  [8:0]             rm_burstcount,
    input  wire                   rm_waitrequest,

    // Write Master
    output reg  [ADDR_WIDTH-1:0]  wm_address,
    output reg                    wm_write,
    output wire [DATA_WIDTH-1:0]  wm_writedata,
    output reg  [8:0]             wm_burstcount,
    input  wire                   wm_waitrequest
);

    // =========================================================================
    // Internal Signals
    // =========================================================================

    // FIFOs
    wire fifo_in_wr_en, fifo_in_rd_en, fifo_in_full, fifo_in_empty;
    wire [DATA_WIDTH-1:0] fifo_in_wr_data, fifo_in_rd_data;
    wire [$clog2(FIFO_DEPTH):0] fifo_in_used;

    reg fifo_out_wr_en;
    reg [DATA_WIDTH-1:0] fifo_out_wr_data;
    wire fifo_out_rd_en, fifo_out_full, fifo_out_empty;
    wire [DATA_WIDTH-1:0] fifo_out_rd_data;
    wire [$clog2(FIFO_DEPTH):0] fifo_out_used;

    // CSR
    reg ctrl_start, ctrl_done_reg, internal_done_pulse;
    reg [ADDR_WIDTH-1:0] ctrl_src_addr, ctrl_dst_addr, ctrl_len;
    reg [31:0] ctrl_coeff;
    reg [8:0] ctrl_rd_burst, ctrl_wr_burst;

    // FSM support
    reg [ADDR_WIDTH-1:0] current_src_addr, current_dst_addr;
    reg [ADDR_WIDTH-1:0] read_remaining_len, remaining_len; 
    reg [ADDR_WIDTH-1:0] pending_reads; 
    
    localparam [1:0] IDLE = 2'b00, READ = 2'b01, WAIT_FIFO = 2'b10;
    localparam [1:0] W_IDLE = 2'b00, W_WAIT_DATA = 2'b01, W_BURST = 2'b10;
    reg [1:0] rm_state, wm_fsm;
    reg [8:0] wm_word_cnt;

    // =========================================================================
    // CSR & FSM (Same as burst_master.v / burst_master_3.v)
    // =========================================================================
    // ... CSR Logic ...
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            ctrl_start <= 0; ctrl_done_reg <= 0;
            ctrl_src_addr <= 0; ctrl_dst_addr <= 0; ctrl_len <= 0;
            ctrl_coeff <= 1; ctrl_rd_burst <= BURST_COUNT; ctrl_wr_burst <= BURST_COUNT;
        end else begin
            if (ctrl_start) ctrl_start <= 0;
            if (internal_done_pulse) ctrl_done_reg <= 1;
            if (avs_write) begin
                case (avs_address)
                    0: if (avs_writedata[0]) ctrl_start <= 1;
                    1: if (avs_writedata[0]) ctrl_done_reg <= 0;
                    2: ctrl_src_addr <= avs_writedata;
                    3: ctrl_dst_addr <= avs_writedata;
                    4: ctrl_len <= (avs_writedata + ((ctrl_rd_burst*4)-1)) & ~((ctrl_rd_burst*4)-1);
                    5: ctrl_rd_burst <= avs_writedata[8:0];
                    6: ctrl_wr_burst <= avs_writedata[8:0];
                    7: ctrl_coeff <= avs_writedata;
                endcase
            end
        end
    end

    always @(*) begin
        case (avs_address)
            0: avs_readdata = {31'b0, ctrl_start};
            1: avs_readdata = {31'b0, ctrl_done_reg};
            2: avs_readdata = ctrl_src_addr;
            3: avs_readdata = ctrl_dst_addr;
            4: avs_readdata = ctrl_len;
            5: avs_readdata = {23'b0, ctrl_rd_burst};
            6: avs_readdata = {23'b0, ctrl_wr_burst};
            7: avs_readdata = ctrl_coeff;
            default: avs_readdata = 0;
        endcase
    end

    // ... Read Master FSM ...
    assign fifo_in_wr_en = rm_readdatavalid;
    assign fifo_in_wr_data = rm_readdata;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            rm_state <= IDLE; rm_address <= 0; rm_read <= 0; rm_burstcount <= BURST_COUNT;
            current_src_addr <= 0; pending_reads <= 0; read_remaining_len <= 0;
        end else begin
            if (rm_state == READ && !rm_waitrequest) 
                pending_reads <= pending_reads + rm_burstcount - (rm_readdatavalid ? 1 : 0);
            else if (rm_readdatavalid && pending_reads > 0) 
                pending_reads <= pending_reads - 1;

            case (rm_state)
                IDLE: if (ctrl_start) begin
                    current_src_addr <= ctrl_src_addr;
                    read_remaining_len <= ctrl_len;
                    rm_state <= WAIT_FIFO;
                end
                WAIT_FIFO: begin
                    if (read_remaining_len > 0) begin
                        if ((fifo_in_used + pending_reads + ctrl_rd_burst) <= FIFO_DEPTH) begin
                            rm_address <= current_src_addr;
                            rm_read <= 1;
                            rm_burstcount <= ctrl_rd_burst;
                            rm_state <= READ;
                        end
                    end
                    if (internal_done_pulse) rm_state <= IDLE;
                end
                READ: if (!rm_waitrequest) begin
                    rm_read <= 0;
                    current_src_addr <= current_src_addr + (ctrl_rd_burst * 4);
                    read_remaining_len <= read_remaining_len - (ctrl_rd_burst * 4);
                    rm_state <= WAIT_FIFO;
                end
            endcase
            if (internal_done_pulse) begin rm_state <= IDLE; pending_reads <= 0; end
        end
    end

    // =========================================================================
    // Handshake Pipeline Logic
    // =========================================================================
    
    // Handshake Signals
    // pipeline_valid[i] : Stage i가 유효한 데이터를 가지고 있음
    // pipeline_ready[i] : Stage i가 새로운 데이터를 받을 준비가 됨
    // pipeline_data[i]  : Stage i의 데이터
    
    // Stage 0 is directly fed by Input FIFO
    // Stage PIPE_LATENCY feeds Output FIFO
    
    reg [DATA_WIDTH-1:0] pipeline_data [0:PIPE_LATENCY]; // 0 to 4
    reg                  pipeline_valid [0:PIPE_LATENCY];
    wire                 pipeline_ready [0:PIPE_LATENCY];
    
    genvar g;
    generate
        // Ready Signal Generation (Back Pressure Propagation)
        // ready[i] = !valid[i] || ready[i+1]
        // (내가 비어있거나, 다음 녀석이 받을 준비가 되면 나는 받을 수 있다)
        for (g = 0; g < PIPE_LATENCY; g = g + 1) begin : gen_ready
            assign pipeline_ready[g] = !pipeline_valid[g] || pipeline_ready[g+1];
        end
    endgenerate

    // 1. Input FIFO -> Stage 0 Interface
    // Input FIFO가 비어있지 않으면(Valid), 그리고 Stage 0가 준비되면(Ready) 읽는다.
    assign fifo_in_rd_en = !fifo_in_empty && pipeline_ready[0];
    
    // 2. Stage Output -> Output FIFO Interface
    // Last Stage Logic
    // Output FIFO가 Full이 아니면 Ready
    assign pipeline_ready[PIPE_LATENCY] = !fifo_out_full;
    
    // Output FIFO Write Logic
    // Last Stage가 Valid하고 Output FIFO가 Ready면 쓴다
    always @(*) begin
        fifo_out_wr_en = pipeline_valid[PIPE_LATENCY] && pipeline_ready[PIPE_LATENCY];
        fifo_out_wr_data = pipeline_data[PIPE_LATENCY];
    end

    // 3. Pipeline Register Update
    integer i;
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            for (i = 0; i <= PIPE_LATENCY; i = i + 1) begin
                pipeline_valid[i] <= 0;
                pipeline_data[i] <= 0;
            end
        end else begin
            // Stage 0 Update (From FIFO)
            if (pipeline_ready[0]) begin
                pipeline_valid[0] <= !fifo_in_empty; // Valid if FIFO not empty
                pipeline_data[0] <= fifo_in_rd_data;
            end
            
            // Stages 1 to PIPE_LATENCY Update
            for (i = 0; i < PIPE_LATENCY; i = i + 1) begin
                if (pipeline_ready[i+1]) begin
                    pipeline_valid[i+1] <= pipeline_valid[i];
                    
                    // Operation Logic
                    if (pipeline_valid[i]) begin
                        // Stage 0 -> 1: Multiply
                        if (i == 0) pipeline_data[i+1] <= pipeline_data[i] * ctrl_coeff; 
                        // Stage 1 -> 2: Multiply
                        else if (i == 1) pipeline_data[i+1] <= (pipeline_data[i] * 64'd5243) >> 21;                         
                        else pipeline_data[i+1] <= pipeline_data[i];
                    end else begin
                        pipeline_data[i+1] <= 0; // Optional clear
                    end
                end
            end
        end
    end

    // ... Write Master FSM & FIFO Instances ...
    // ... Same as previous versions ...

    // =========================================================================
    // Write Master FSM
    // =========================================================================
    assign fifo_out_rd_en = (wm_fsm == W_BURST) && (!wm_waitrequest) && (wm_word_cnt < wm_burstcount);
    assign wm_writedata = fifo_out_rd_data;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            wm_fsm <= W_IDLE; wm_write <= 0; wm_word_cnt <= 0; wm_address <= 0;
            current_dst_addr <= 0; remaining_len <= 0; internal_done_pulse <= 0; wm_burstcount <= BURST_COUNT;
        end else begin
            internal_done_pulse <= 0;
            case (wm_fsm)
                W_IDLE: begin
                    wm_write <= 0;
                    if (ctrl_start) begin
                        current_dst_addr <= ctrl_dst_addr;
                        remaining_len <= ctrl_len;
                        wm_fsm <= W_WAIT_DATA;
                    end
                end
                W_WAIT_DATA: begin
                    wm_write <= 0;
                    if (remaining_len == 0) begin
                        internal_done_pulse <= 1;
                        wm_fsm <= W_IDLE;
                    end else if (fifo_out_used >= ctrl_wr_burst) begin
                        wm_address <= current_dst_addr;
                        wm_burstcount <= ctrl_wr_burst;
                        wm_write <= 1; wm_word_cnt <= 0;
                        wm_fsm <= W_BURST;
                    end
                end
                W_BURST: begin
                    if (!wm_waitrequest) begin
                        if (wm_word_cnt == wm_burstcount - 1) begin
                            wm_write <= 0;
                            current_dst_addr <= current_dst_addr + (wm_burstcount * 4);
                            remaining_len <= remaining_len - (wm_burstcount * 4);
                            wm_fsm <= W_WAIT_DATA;
                        end else begin
                            wm_word_cnt <= wm_word_cnt + 1;
                        end
                    end
                end
            endcase
        end
    end

    simple_fifo #(.DATA_WIDTH(DATA_WIDTH), .FIFO_DEPTH(FIFO_DEPTH)) u_fifo_in (
        .clk(clk), .rst_n(reset_n),
        .wr_en(fifo_in_wr_en), .wr_data(fifo_in_wr_data),
        .rd_en(fifo_in_rd_en), .rd_data(fifo_in_rd_data),
        .full(fifo_in_full), .empty(fifo_in_empty), .used_w(fifo_in_used)
    );

    simple_fifo #(.DATA_WIDTH(DATA_WIDTH), .FIFO_DEPTH(FIFO_DEPTH)) u_fifo_out (
        .clk(clk), .rst_n(reset_n),
        .wr_en(fifo_out_wr_en), .wr_data(fifo_out_wr_data),
        .rd_en(fifo_out_rd_en), .rd_data(fifo_out_rd_data),
        .full(fifo_out_full), .empty(fifo_out_empty), .used_w(fifo_out_used)
    );

endmodule
