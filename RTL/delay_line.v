`timescale 1ns/1ps

module delay_line #(
    parameter WIDTH  = 1,  // 데이터의 비트 폭 (예: 1, 8, 24, 32...)
    parameter STAGES = 1   // 지연시킬 클럭 수 (예: 1클럭, 2클럭...)
)(
    input  wire             clk,
    input  wire             reset_n,
    input  wire [WIDTH-1:0] din,
    output wire [WIDTH-1:0] dout
);

    generate
        // 1. 지연이 필요 없는 경우 (STAGES == 0) -> 바로 통과 (Bypass)
        if (STAGES == 0) begin : gen_no_delay
            assign dout = din;
            
        // 2. 지연이 필요한 경우 -> 시프트 레지스터(D-FF 배열) 생성
        end else begin : gen_delay
            reg [WIDTH-1:0] shift_reg [0:STAGES-1];
            integer i;

            always @(posedge clk or negedge reset_n) begin
                if (!reset_n) begin
                    // 리셋 시 모든 단계를 0으로 초기화
                    for (i = 0; i < STAGES; i = i + 1) begin
                        shift_reg[i] <= {WIDTH{1'b0}};
                    end
                end else begin
                    // 첫 번째 단에 입력 넣기
                    shift_reg[0] <= din;
                    // 이전 단의 데이터를 다음 단으로 넘기기 (컨베이어 벨트)
                    for (i = 1; i < STAGES; i = i + 1) begin
                        shift_reg[i] <= shift_reg[i-1];
                    end
                end
            end
            
            // 마지막 단의 데이터를 출력
            assign dout = shift_reg[STAGES-1];
        end
    endgenerate

endmodule
