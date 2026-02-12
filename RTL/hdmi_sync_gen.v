
// 720p (1280x720 @ 60Hz) HDMI Sync Generator
// Pixel Clock: 74.25 MHz

module hdmi_sync_gen (
    input  wire        clk,      // 74.25 MHz
    input  wire        reset_n,
    
    // HDMI Signals
    output reg  [23:0] hdmi_d,
    output reg         hdmi_de,
    output reg         hdmi_hs,
    output reg         hdmi_vs,

    // Avalon-MM Slave Interface for Control
    input  wire [1:0]  avs_address,
    input  wire        avs_read,
    input  wire        avs_write,
    input  wire [31:0] avs_writedata,
    output wire [31:0] avs_readdata,
    output reg         avs_readdatavalid
);

    // Control Registers
    reg [31:0] reg_mode;       // Addr 0: Mode selection
    reg [31:0] reg_gamma_ctrl; // Addr 1: Bit 0 = Gamma Enable
    reg [31:0] reg_lut_addr;   // Addr 2: LUT Address (0-255)
    reg [31:0] reg_lut_data;   // Addr 3: LUT Data (8-bit)

    // LUT Memory (256x8)
    reg [7:0] lut_mem [0:255];

    // Read Logic: Explicit Case for Address Decoding
    reg [31:0] read_data_mux;
    assign avs_readdata = read_data_mux;

    always @(*) begin
        case (avs_address)
            2'd0:    read_data_mux = reg_mode;
            2'd1:    read_data_mux = reg_gamma_ctrl;
            2'd2:    read_data_mux = reg_lut_addr;
            2'd3:    read_data_mux = reg_lut_data;
            default: read_data_mux = 32'd0;
        endcase
    end

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            reg_mode <= 32'd0;
            reg_gamma_ctrl <= 32'd0; // Default: Gamma Disabled
            reg_lut_addr <= 32'd0;
            reg_lut_data <= 32'd0;
            avs_readdatavalid <= 1'b0;
        end else begin
            // Write Logic
            if (avs_write) begin
                case (avs_address)
                    2'd0: reg_mode <= avs_writedata;
                    2'd1: reg_gamma_ctrl <= avs_writedata;
                    2'd2: reg_lut_addr <= avs_writedata;
                    2'd3: begin
                        reg_lut_data <= avs_writedata;
                        lut_mem[reg_lut_addr[7:0]] <= avs_writedata[7:0];
                    end
                endcase
            end
            
            // Read Valid Logic (1-cycle latency)
            avs_readdatavalid <= avs_read;
        end
    end

    // 720p Timing Parameters
    parameter H_VISIBLE    = 1280;
    parameter H_FRONT      = 110;
    parameter H_SYNC       = 40;
    parameter H_BACK       = 220;
    parameter H_TOTAL      = 1650;

    parameter V_VISIBLE    = 720;
    parameter V_FRONT      = 5;
    parameter V_SYNC       = 5;
    parameter V_BACK       = 20;
    parameter V_TOTAL      = 750;

    reg [11:0] h_cnt;
    reg [11:0] v_cnt;

    // Horizontal Counter
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n)
            h_cnt <= 12'd0;
        else if (h_cnt == H_TOTAL - 1)
            h_cnt <= 12'd0;
        else
            h_cnt <= h_cnt + 12'd1;
    end

    // Vertical Counter
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n)
            v_cnt <= 12'd0;
        else if (h_cnt == H_TOTAL - 1) begin
            if (v_cnt == V_TOTAL - 1)
                v_cnt <= 12'd0;
            else
                v_cnt <= v_cnt + 12'd1;
        end
    end

    // Sync & DE Generation
    always @(posedge clk) begin
        hdmi_hs <= (h_cnt >= (H_VISIBLE + H_FRONT) && h_cnt < (H_VISIBLE + H_FRONT + H_SYNC));
        hdmi_vs <= (v_cnt >= (V_VISIBLE + V_FRONT) && v_cnt < (V_VISIBLE + V_FRONT + V_SYNC));
        hdmi_de <= (h_cnt < H_VISIBLE && v_cnt < V_VISIBLE);
    end

    // Pixel Data Generation Based on Mode
    wire [7:0] plain_r, plain_g, plain_b;
    reg  [23:0] pre_gamma_d;

    // LUT Logic (Apply only if Gamma Enable is 1)
    wire [7:0] gamma_r = lut_mem[pre_gamma_d[23:16]];
    wire [7:0] gamma_g = lut_mem[pre_gamma_d[15:8]];
    wire [7:0] gamma_b = lut_mem[pre_gamma_d[7:0]];

    wire [7:0] gray = h_cnt[7:0];
    wire grid_line = (h_cnt[5:0] == 6'd0) || (v_cnt[5:0] == 6'd0);
    wire [2:0] bar_idx = (h_cnt < 160)  ? 3'd0 :
                         (h_cnt < 320)  ? 3'd1 :
                         (h_cnt < 480)  ? 3'd2 :
                         (h_cnt < 640)  ? 3'd3 :
                         (h_cnt < 800)  ? 3'd4 :
                         (h_cnt < 960)  ? 3'd5 :
                         (h_cnt < 1120) ? 3'd6 : 3'd7;
    wire [7:0] gray8_val = {bar_idx, 5'd0}; // Each step is 32

    always @(*) begin
        case (reg_mode[2:0])
            3'd0: pre_gamma_d = 24'hFF0000; // Red
            3'd1: pre_gamma_d = 24'h00FF00; // Green
            3'd2: pre_gamma_d = 24'h0000FF; // Blue
            3'd3: pre_gamma_d = {gray, gray, gray}; // Grayscale Ramp
            3'd4: pre_gamma_d = grid_line ? 24'hFFFFFF : 24'h000000; // Grid
            3'd5: pre_gamma_d = 24'hFFFFFF; // Solid White
            3'd6: pre_gamma_d = {gray8_val, gray8_val, gray8_val}; // 8-level Gray Scale
            default: pre_gamma_d = 24'hFFFFFF; // White
        endcase
    end

    always @(posedge clk) begin
        if (hdmi_de) begin
            if (reg_gamma_ctrl[0])
                hdmi_d <= {gamma_r, gamma_g, gamma_b};
            else
                hdmi_d <= pre_gamma_d;
        end else begin
            hdmi_d <= 24'h000000; // Blank
        end
    end

endmodule
