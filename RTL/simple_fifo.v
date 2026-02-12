/*
 * 모듈명: simple_fifo
 * 
 * [개요]
 * FWFT (First-Word-Fall-Through) 방식의 동기식 FIFO입니다.
 * 일반적인 FIFO와 달리, FWFT 방식은 데이터가 도착하면 즉시 출력 포트에 나타나므로
 * Read Latency가 0이 되어 고속 데이터 처리에 유리합니다.
 * 
 * [FWFT 동작 원리]
 * - 일반 FIFO: rd_en을 주고 1 클럭 후 rd_data가 유효해짐 (Latency=1)
 * - FWFT FIFO: rd_data는 항상 유효함. rd_en은 "데이터를 소비했음"을 알리는 Ack 신호
 * 
 * [Circular Buffer 구조]
 * - wr_ptr: 다음 쓸 위치를 가리킴
 * - rd_ptr: 현재 읽을 데이터 위치를 가리킴
 * - used_w: FIFO에 저장된 유효 데이터 개수
 * 
 * [사용 예시]
 * 1. Write: wr_en=1이면 wr_data를 FIFO에 저장
 * 2. Read: !empty이면 rd_data는 유효. rd_en=1로 소비 확인
 */

module simple_fifo #(
    parameter DATA_WIDTH = 32,   // 데이터 비트폭
    parameter FIFO_DEPTH = 512   // FIFO 깊이 (저장 가능한 데이터 개수)
)(
    input  wire                   clk,
    input  wire                   rst_n,
    
    // Write Interface
    input  wire                   wr_en,     // Write Enable
    input  wire [DATA_WIDTH-1:0]  wr_data,   // Write Data
    
    // Read Interface (FWFT)
    input  wire                   rd_en,     // Read Acknowledge (데이터 소비 확인)
    output wire [DATA_WIDTH-1:0]  rd_data,   // Read Data (항상 유효, !empty일 때)
    
    // Status
    output wire                   full,      // FIFO Full 플래그
    output wire                   empty,     // FIFO Empty 플래그
    output reg  [$clog2(FIFO_DEPTH):0] used_w // 현재 저장된 데이터 개수
);

    // =========================================================================
    // Internal Signals
    // =========================================================================
    localparam ADDR_WIDTH = $clog2(FIFO_DEPTH);

    reg [DATA_WIDTH-1:0] mem [0:FIFO_DEPTH-1];  // FIFO Memory (Circular Buffer)
    reg [ADDR_WIDTH-1:0] wr_ptr;                // Write Pointer
    reg [ADDR_WIDTH-1:0] rd_ptr;                // Read Pointer

    // =========================================================================
    // Status Flags
    // =========================================================================
    assign full  = (used_w == FIFO_DEPTH);  // 모든 공간이 찼을 때
    assign empty = (used_w == 0);           // 데이터가 하나도 없을 때
    
    // =========================================================================
    // FWFT Read Data
    // =========================================================================
    // FWFT 특성: rd_data는 항상 rd_ptr 위치의 데이터를 출력
    // empty가 아니면 이 데이터는 유효함
    assign rd_data = mem[rd_ptr];

    // =========================================================================
    // Main Logic
    // =========================================================================
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            // 리셋 시 모든 포인터와 카운터 초기화
            wr_ptr <= 0;
            rd_ptr <= 0;
            used_w <= 0;
        end else begin
            // =====================================================================
            // Write Operation
            // =====================================================================
            // full이 아닐 때만 쓰기 가능
            if (wr_en && !full) begin
                mem[wr_ptr] <= wr_data;  // 현재 wr_ptr 위치에 데이터 저장
                
                // Circular Buffer: 마지막에 도달하면 0으로 Wrap-around
                wr_ptr <= (wr_ptr == FIFO_DEPTH-1) ? 0 : wr_ptr + 1;
            end

            // =====================================================================
            // Read Operation (Acknowledge)
            // =====================================================================
            // empty가 아닐 때만 읽기 가능
            // FWFT이므로 rd_en은 "데이터를 소비했음"을 알리는 ACK
            if (rd_en && !empty) begin
                // 데이터는 이미 rd_data에 출력되어 있으므로, 포인터만 증가
                rd_ptr <= (rd_ptr == FIFO_DEPTH-1) ? 0 : rd_ptr + 1;
            end

            // =====================================================================
            // Usage Counter Update
            // =====================================================================
            // used_w는 FIFO에 저장된 유효 데이터의 개수
            // 동시 읽기/쓰기 시에는 증감이 상쇄될 수 있으므로 조건 분기 필요
            
            // Case 1: Write만 발생 (Read 없음 또는 Empty라 Read 무효)
            if (wr_en && !full && (!rd_en || empty)) begin
                used_w <= used_w + 1;
            end 
            // Case 2: Read만 발생 (Write 없음 또는 Full이라 Write 무효)
            else if (rd_en && !empty && (!wr_en || full)) begin
                used_w <= used_w - 1;
            end
            // Case 3: 동시 Read/Write가 모두 유효하면 used_w 유지 (증감 상쇄)
        end
    end

endmodule
