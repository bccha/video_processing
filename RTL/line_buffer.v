`timescale 1ns/1ps

// 1줄의 픽셀(H_TOTAL 개수)을 저장하고 1 라인만큼 지연시켜 출력하는 모듈
module line_buffer #(
    parameter DATA_WIDTH = 24, // 픽셀 데이터 비트 폭 (RGB 888)
    parameter LINE_LENGTH = 1120 // 1줄당 총 픽셀 수 (H_TOTAL, 960+160)
)(
    input  wire                  clk,
    input  wire                  reset_n,
    
    // 쓰기(입력) 측
    input  wire                  wr_en,      // 데이터 유효할 때 (픽셀이 들어올 때 보통 1)
    input  wire [DATA_WIDTH-1:0] din,        // 현재 입력 픽셀
    
    // 읽기(출력) 측
    output wire [DATA_WIDTH-1:0] dout        // 1 라인 길이(LINE_LENGTH)만큼 지연된 픽셀
);

    // 어드레스 비트 폭 계산 (1120을 담으려면 2^11=2048, 즉 11비트 필요)
    localparam ADDR_WIDTH = 11;

    // 1줄 분량의 FPGA Block RAM (Inferred RAM)
    reg [DATA_WIDTH-1:0] ram [0:LINE_LENGTH-1];
    
    // Circular Buffer 용 쓰기/읽기 포인터
    reg [ADDR_WIDTH-1:0] ptr;
    
    // 포인터 증가 로직
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            ptr <= {ADDR_WIDTH{1'b0}};
        end else if (wr_en) begin
            // 끝에 도달하면 0으로 롤오버
            if (ptr == LINE_LENGTH - 1)
                ptr <= {ADDR_WIDTH{1'b0}};
            else
                ptr <= ptr + 1'b1;
        end
    end

    // RAM 읽기 및 쓰기
    // Read-Before-Write 방식: 같은 주소에서 먼저 과거 값을 꺼내고, 빈자리에 새 값을 넣음
    reg [DATA_WIDTH-1:0] r_dout;
    
    always @(posedge clk) begin
        if (wr_en) begin
            // 1. 읽기 (LINE_LENGTH 전의 과거 데이터가 나옴)
            r_dout <= ram[ptr];
            
            // 2. 쓰기 (방금 꺼낸 빈 주소에 새로운 현재 데이터 기록)
            ram[ptr] <= din;
        end
    end
    
    // 외부 출력
    assign dout = r_dout;

endmodule
