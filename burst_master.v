`timescale 1ns/1ps

/*
 * ============================================================================
 * 모듈명: burst_master (기본 DMA 컨트롤러)
 * ============================================================================
 * 
 * [목적]
 * Avalon Memory-Mapped (Avalon-MM) 인터페이스를 사용하여 메모리 간 고속 데이터 복사를 
 * 수행하는 DMA (Direct Memory Access) 컨트롤러입니다.
 * CPU의 개입 없이 대용량 데이터를 Source에서 Destination으로 직접 전송합니다.
 * 
 * [핵심 개념: Burst Transfer]
 * 일반적인 메모리 읽기/쓰기는 "주소 전송 -> 데이터 1개 전송"을 반복합니다.
 * 하지만 Burst Transfer는 "주소 1번 전송 -> 데이터 N개 연속 전송"이 가능합니다.
 * 
 * 예시:
 * - 일반 전송: [Addr0]-[Data0] [Addr1]-[Data1] [Addr2]-[Data2] ...
 * - Burst 전송: [Addr0 + BurstCount=256]-[Data0][Data1][Data2]...[Data255]
 * 
 * 이렇게 하면 주소 전송 오버헤드가 1/256로 줄어들어 버스 효율이 크게 향상됩니다.
 * 
 * [아키텍처: FIFO 기반 Read/Write 분리]
 * 
 *     Read Master ──┐
 *                   │ (Avalon-MM Read)
 *     Memory Bus ───┤
 *                   │ (Avalon-MM Write)
 *     Write Master ─┘
 *          ↑
 *          │ (FIFO로 데이터 버퍼링)
 *          ↓
 *     [FIFO Buffer]
 * 
 * Read Master와 Write Master 사이에 FIFO를 두어:
 * 1. Read와 Write가 독립적으로 동작 가능 (병렬화)
 * 2. Read/Write 속도 차이를 완충 (Buffering)
 * 3. 버스 대역폭 최대한 활용
 * 
 * [제어 인터페이스: Avalon-MM CSR Slave]
 * CPU(Nios II)가 레지스터를 통해 DMA를 제어합니다:
 * - 0x0: Control (Start 명령)
 * - 0x1: Status (Done 확인)
 * - 0x2: Source Address
 * - 0x3: Destination Address
 * - 0x4: Length (Bytes)
 * 
 * [동작 시퀀스]
 * 1. CPU가 Source/Dest 주소와 Length를 설정
 * 2. Start 비트를 1로 설정
 * 3. Read Master: FIFO 공간 확인 후 메모리에서 읽기 시작
 * 4. Write Master: FIFO 데이터 확인 후 메모리에 쓰기 시작
 * 5. 모든 데이터 전송 완료 후 Done 플래그 설정
 * 6. CPU가 Done을 확인하고 다음 작업 진행
 */

module burst_master #(
    parameter DATA_WIDTH = 32,      // 32비트 데이터 버스
    parameter ADDR_WIDTH = 32,      // 32비트 주소 버스 (4GB 주소 공간)
    parameter BURST_COUNT = 256,    // Burst당 256 워드 = 1KB
    parameter FIFO_DEPTH = 512      // 512 워드 = 2KB FIFO
)(
    input  wire                   clk,
    input  wire                   reset_n,

    // =========================================================================
    // Avalon-MM CSR Slave (제어/상태 레지스터)
    // =========================================================================
    // CPU가 이 인터페이스를 통해 DMA를 제어합니다.
    input  wire                   avs_write,      // 레지스터 쓰기 요청
    input  wire                   avs_read,       // 레지스터 읽기 요청
    input  wire [2:0]             avs_address,    // 레지스터 주소 (0~7)
    input  wire [31:0]            avs_writedata,  // 쓸 데이터
    output reg  [31:0]            avs_readdata,   // 읽은 데이터

    // =========================================================================
    // Avalon-MM Read Master (메모리 읽기)
    // =========================================================================
    // 이 모듈이 Master로서 메모리에 읽기 명령을 보냅니다.
    output reg  [ADDR_WIDTH-1:0]  rm_address,       // 읽을 주소
    output reg                    rm_read,          // 읽기 요청
    input  wire [DATA_WIDTH-1:0]  rm_readdata,      // 읽은 데이터 (Slave 응답)
    input  wire                   rm_readdatavalid, // 데이터 유효 신호
    output reg  [8:0]             rm_burstcount,    // Burst 길이 (가변)
    input  wire                   rm_waitrequest,   // Slave 대기 요청

    // =========================================================================
    // Avalon-MM Write Master (메모리 쓰기)
    // =========================================================================
    // 이 모듈이 Master로서 메모리에 쓰기 명령을 보냅니다.
    output reg  [ADDR_WIDTH-1:0]  wm_address,       // 쓸 주소
    output reg                    wm_write,         // 쓰기 요청
    output wire [DATA_WIDTH-1:0]  wm_writedata,     // 쓸 데이터
    output reg  [8:0]             wm_burstcount,    // Burst 길이 (가변)
    input  wire                   wm_waitrequest    // Slave 대기 요청
);

    // =========================================================================
    // 내부 신호 및 레지스터
    // =========================================================================

    // -----------------------------------------------------------------
    // CSR (Control and Status Registers)
    // -----------------------------------------------------------------
    reg                   ctrl_start;       // 전송 시작 명령 (Pulse)
    reg                   ctrl_done_reg;    // 전송 완료 플래그
    reg [ADDR_WIDTH-1:0]  ctrl_src_addr;    // Source 주소
    reg [ADDR_WIDTH-1:0]  ctrl_dst_addr;    // Destination 주소
    reg [ADDR_WIDTH-1:0]  ctrl_len;         // 전송 길이 (Bytes)
    
    // [New] Programmable Burst Counts
    reg [8:0]             ctrl_rd_burst;    // Read Master Burst Count
    reg [8:0]             ctrl_wr_burst;    // Write Master Burst Count

    // -----------------------------------------------------------------
    // FIFO 인터페이스 신호
    // -----------------------------------------------------------------
    // Read Master -> FIFO -> Write Master 데이터 흐름을 연결합니다.
    wire                   fifo_wr_en;      // FIFO 쓰기 Enable (Read가 데이터 받으면 1)
    wire [DATA_WIDTH-1:0]  fifo_wr_data;    // FIFO에 쓸 데이터
    wire                   fifo_rd_en;      // FIFO 읽기 Enable (Write가 데이터 보낼 때 1)
    wire [DATA_WIDTH-1:0]  fifo_rd_data;    // FIFO에서 읽은 데이터
    wire                   fifo_full;       // FIFO 가득 참
    wire                   fifo_empty;      // FIFO 비어 있음
    wire [$clog2(FIFO_DEPTH):0] fifo_used;  // FIFO에 저장된 워드 수

    // -----------------------------------------------------------------
    // Read Master 제어 신호
    // -----------------------------------------------------------------
    reg [ADDR_WIDTH-1:0] current_src_addr;      // 현재 읽기 주소
    reg [ADDR_WIDTH-1:0] read_remaining_len;    // 남은 읽기 길이 (Bytes)
    reg [ADDR_WIDTH-1:0] pending_reads;         // In-flight Read 개수 추적

    // -----------------------------------------------------------------
    // Write Master 제어 신호
    // -----------------------------------------------------------------
    reg [ADDR_WIDTH-1:0] current_dst_addr;      // 현재 쓰기 주소
    reg [ADDR_WIDTH-1:0] remaining_len;         // 남은 쓰기 길이 (Bytes)

    // -----------------------------------------------------------------
    // 내부 제어 신호
    // -----------------------------------------------------------------
    reg internal_done_pulse;  // 전송 완료 시 1 클럭 동안 High

    // =========================================================================
    // CSR 로직 (Avalon-MM Slave 동작)
    // =========================================================================
    /*
     * CPU가 레지스터에 접근할 때의 동작을 정의합니다.
     * 
     * [주소 맵]
     * 0: Control (Start=Bit0)
     * 1: Status (Done=Bit0, W1C)
     * 2: Source Address
     * 3: Destination Address
     * 4: Length (Bytes, with Padding)
     * 5: Read Burst Count (Default: Parameter BURST_COUNT)
     * 6: Write Burst Count (Default: Parameter BURST_COUNT)
     */
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            ctrl_start    <= 0;
            ctrl_done_reg <= 0;
            ctrl_src_addr <= 0;
            ctrl_dst_addr <= 0;
            ctrl_len      <= 0;
            ctrl_rd_burst <= BURST_COUNT; // Default Reset Value
            ctrl_wr_burst <= BURST_COUNT; // Default Reset Value
        end else begin
            // Start Pulse Auto-Clear: 1 클럭 후 자동으로 0
            if (ctrl_start) ctrl_start <= 0;

            // Done Flag Set: FSM이 완료 시 Pulse를 보내면 플래그 설정
            if (internal_done_pulse) begin
                ctrl_done_reg <= 1;
            end

            // Avalon-MM Write 처리
            if (avs_write) begin
                case (avs_address)
                    3'd0: begin // Control Register
                        if (avs_writedata[0]) ctrl_start <= 1; // Start Command
                    end
                    3'd1: begin // Status Register
                        if (avs_writedata[0]) ctrl_done_reg <= 0; // Clear Done
                    end
                    3'd2: ctrl_src_addr <= avs_writedata;  // Source Address
                    3'd3: ctrl_dst_addr <= avs_writedata;  // Destination Address
                    3'd4: begin // Length (Bytes)
                        // Burst 단위(Byte)로 정렬 (Padding)하여 Underrun 방지
                        // 예: 1000 -> 1024, 1025 -> 2048
                        // 수식: (len + (Burst*4 - 1)) & ~(Burst*4 - 1)
                        // 주의: 사용자가 Read Burst Count를 먼저 설정해야 정확함
                        ctrl_len <= (avs_writedata + ((ctrl_rd_burst*4)-1)) & ~((ctrl_rd_burst*4)-1); 
                    end
                    3'd5: ctrl_rd_burst <= avs_writedata[8:0]; // Set Read Burst Count
                    3'd6: ctrl_wr_burst <= avs_writedata[8:0]; // Set Write Burst Count
                endcase
            end
        end
    end

    // Avalon-MM Read 처리 (Combinational)
    always @(*) begin
        case (avs_address)
            3'd0: avs_readdata = {31'b0, ctrl_start};
            3'd1: avs_readdata = {31'b0, ctrl_done_reg};
            3'd2: avs_readdata = ctrl_src_addr;
            3'd3: avs_readdata = ctrl_dst_addr;
            3'd4: avs_readdata = ctrl_len;
            3'd5: avs_readdata = {23'b0, ctrl_rd_burst};
            3'd6: avs_readdata = {23'b0, ctrl_wr_burst};
            default: avs_readdata = 32'b0;
        endcase
    end

    // =========================================================================
    // Read Master FSM
    // =========================================================================
    /*
     * [역할]
     * 메모리에서 데이터를 읽어 FIFO에 채우는 역할을 합니다.
     * 
     * [변경점]
     * 고정된 BURST_COUNT 대신 프로그래머블 ctrl_rd_burst를 사용합니다.
     */

    localparam [1:0] IDLE = 2'b00,
                     READ = 2'b01,
                     WAIT_FIFO = 2'b10;

    reg [1:0] rm_state;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            rm_state <= IDLE;
            rm_address <= 0;
            rm_read <= 0;
            rm_burstcount <= BURST_COUNT; // Initial value
            current_src_addr <= 0;
            pending_reads <= 0;
            read_remaining_len <= 0;
        end else begin
            // -----------------------------------------------------------------
            // Pending Read Counter 업데이트
            // -----------------------------------------------------------------
            // 케이스 1: Read 명령 수락 (READ state & !waitrequest)
            //   -> pending_reads에 ctrl_rd_burst 추가 (Use latched rm_burstcount)
            // 케이스 2: 데이터 수신 (readdatavalid)
            //   -> pending_reads에서 1 차감
            // 케이스 3: 동시 발생
            //   -> rm_burstcount - 1 추가 (사실 rm_burstcount는 READ 상태에서 ctrl_rd_burst와 같음)
            if (rm_state == READ && !rm_waitrequest) begin
                pending_reads <= pending_reads + rm_burstcount - (rm_readdatavalid ? 1 : 0);
            end else if (rm_readdatavalid) begin
                if (pending_reads > 0)
                    pending_reads <= pending_reads - 1;
            end

            // -----------------------------------------------------------------
            // State Machine
            // -----------------------------------------------------------------
            case (rm_state)
                IDLE: begin
                    if (ctrl_start) begin
                        // 전송 시작: 주소와 길이 래치
                        current_src_addr <= ctrl_src_addr;
                        read_remaining_len <= ctrl_len;
                        rm_state <= WAIT_FIFO;
                    end
                end

                WAIT_FIFO: begin
                    // 아직 읽을 데이터가 남았는지 확인
                    if (read_remaining_len > 0) begin
                        // FIFO 공간 체크: (현재 사용량 + 대기 중 + 새 요청) <= 전체 깊이
                        if ((fifo_used + pending_reads + ctrl_rd_burst) <= FIFO_DEPTH) begin
                            // 공간 충분: Read 명령 준비
                            rm_address <= current_src_addr;
                            rm_read <= 1;
                            rm_burstcount <= ctrl_rd_burst; // Use dynamic burst count
                            rm_state <= READ;
                        end
                        // 공간 부족: 대기 (FIFO가 비워질 때까지)
                    end
                    // read_remaining_len == 0: 더 이상 읽을 것 없음, Write 완료 대기
                end

                READ: begin
                    // Avalon-MM 프로토콜: waitrequest가 0일 때 명령 수락됨
                    if (!rm_waitrequest) begin
                        rm_read <= 0;  // 명령 전송 완료, 신호 내림
                        
                        // 다음 Burst를 위한 주소/길이 갱신
                        // 주소: +Burst 워드
                        current_src_addr <= current_src_addr + (ctrl_rd_burst * 4);
                        read_remaining_len <= read_remaining_len - (ctrl_rd_burst * 4);
                        
                        rm_state <= WAIT_FIFO;  // 다시 공간 확인으로
                    end
                    // waitrequest == 1: Slave가 Busy, 대기
                end
            endcase
            
            // 전체 작업 완료 시 초기화
            if (internal_done_pulse) begin
                rm_state <= IDLE;
                pending_reads <= 0;
            end
        end
    end

    // FIFO Write 연결: Read Master가 데이터를 받으면 FIFO에 씁니다.
    assign fifo_wr_en = rm_readdatavalid;
    assign fifo_wr_data = rm_readdata;

    // =========================================================================
    // Write Master FSM
    // =========================================================================
    /*
     * [역할]
     * FIFO에서 데이터를 읽어 메모리에 쓰는 역할을 합니다.
     * 
     * [변경점]
     * 고정된 BURST_COUNT 대신 프로그래머블 ctrl_wr_burst를 사용합니다.
     */

    localparam [1:0] W_IDLE = 2'b00,
                     W_WAIT_DATA = 2'b01,
                     W_BURST = 2'b10;

    reg [1:0] wm_fsm;
    reg [8:0] wm_word_cnt;  // Burst 내 전송된 워드 수

    // FIFO Read 제어: Burst 중이고, Slave가 준비되었고, 아직 다 안 보냈으면 읽기
    // 주의: wm_burstcount는 W_BURST 진입 시 ctrl_wr_burst로 래치됨
    assign fifo_rd_en = (wm_fsm == W_BURST) && (!wm_waitrequest) && (wm_word_cnt < wm_burstcount);
    
    // Write Data는 FIFO 출력을 바로 연결
    assign wm_writedata = fifo_rd_data;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            wm_fsm <= W_IDLE;
            wm_write <= 0;
            wm_word_cnt <= 0;
            wm_address <= 0;
            current_dst_addr <= 0;
            remaining_len <= 0;
            internal_done_pulse <= 0;
            wm_burstcount <= BURST_COUNT; // Initial value
        end else begin
            internal_done_pulse <= 0;  // Default: Pulse는 1 클럭만 High
            
            case (wm_fsm)
                W_IDLE: begin
                    wm_write <= 0;
                    if (ctrl_start) begin
                        // 전송 시작: 목적지 주소와 길이 래치
                        current_dst_addr <= ctrl_dst_addr;
                        remaining_len <= ctrl_len;
                        wm_fsm <= W_WAIT_DATA;
                    end
                end
                
                W_WAIT_DATA: begin
                    wm_write <= 0;
                    
                    if (remaining_len == 0) begin
                        // 모든 데이터 전송 완료!
                        internal_done_pulse <= 1;  // Done 플래그 설정
                        wm_fsm <= W_IDLE;
                    end else if (fifo_used >= ctrl_wr_burst) begin
                        // FIFO에 Burst 분량만큼 데이터가 준비됨
                        wm_address <= current_dst_addr;
                        wm_burstcount <= ctrl_wr_burst; // Use dynamic burst count
                        wm_write <= 1;  // Burst 시작 (FIFO FWFT라 데이터 이미 준비됨)
                        wm_word_cnt <= 0;
                        wm_fsm <= W_BURST;
                    end
                    // FIFO에 데이터 부족: 대기 (Read Master가 채울 때까지)
                end
                
                W_BURST: begin
                    // Burst 전송 진행
                    if (!wm_waitrequest) begin
                        // Slave가 데이터 수락
                        if (wm_word_cnt == wm_burstcount - 1) begin
                            // 마지막 워드 전송 완료
                            wm_write <= 0;
                            
                            // 다음 Burst를 위한 주소/길이 갱신
                            current_dst_addr <= current_dst_addr + (wm_burstcount * 4);
                            remaining_len <= remaining_len - (wm_burstcount * 4);
                            
                            wm_fsm <= W_WAIT_DATA;
                        end else begin
                            // 아직 더 전송해야 함
                            wm_word_cnt <= wm_word_cnt + 1;
                            // wm_write는 계속 High 유지
                        end
                    end
                    // waitrequest == 1: Slave Busy, wm_writedata 유지하고 대기
                end
            endcase
        end
    end

    // =========================================================================
    // FIFO 인스턴스
    // =========================================================================
    /*
     * Read Master와 Write Master 사이의 데이터 버퍼입니다.
     * 
     * [FIFO 역할]
     * 1. 속도 완충: Read가 빠르고 Write가 느릴 때 데이터를 임시 저장
     * 2. 병렬화: Read와 Write가 독립적으로 동작 가능
     * 3. Burst 모으기: Write는 BURST_COUNT만큼 데이터가 모일 때까지 대기
     * 
     * [FIFO 크기 선택]
     * FIFO_DEPTH = 512 = 2 * BURST_COUNT
     * - Read Burst 2개분을 저장할 수 있음
     * - Write가 한 Burst 쓰는 동안 Read가 다음 Burst를 준비 가능
     */
    simple_fifo #(
        .DATA_WIDTH(DATA_WIDTH),
        .FIFO_DEPTH(FIFO_DEPTH)
    ) u_fifo (
        .clk     (clk),
        .rst_n   (reset_n),
        .wr_en   (fifo_wr_en),
        .wr_data (fifo_wr_data),
        .rd_en   (fifo_rd_en),
        .rd_data (fifo_rd_data),
        .full    (fifo_full),
        .empty   (fifo_empty),
        .used_w  (fifo_used)
    );

endmodule
