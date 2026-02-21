`timescale 1ns/1ps

module filter_error_diffusion #(
    parameter DATA_WIDTH = 24
)(
    input  wire                  clk,
    input  wire                  reset_n,
    
    // Control
    input  wire                  error_diffusion_en,
    input  wire [7:0]            dither_threshold,
    
    // Pixel Input
    input  wire [DATA_WIDTH-1:0] din,
    input  wire                  hs_in,
    input  wire                  vs_in,
    input  wire                  de_in,
    
    // Output
    output reg  [DATA_WIDTH-1:0] dout,
    output reg                   hs_out,
    output reg                   vs_out,
    output reg                   de_out
);

    // ==========================================
    // 0. Coordinate Tracking
    // ==========================================
    reg [11:0] x_coord;
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            x_coord <= 0;
        end else begin
            if (!de_in)
                x_coord <= 0;
            else
                x_coord <= x_coord + 1;
        end
    end

    // ==========================================
    // 1. Error Line Buffer (BRAM)
    // ==========================================
    // We store 3 channels of 9-bit Signed Error -> 27 bits total
    // Depth: 1024 (covers H_VISIBLE = 960)
    reg [26:0] err_bram [0:1023];
    reg [26:0] bram_rd_data;
    
    wire [11:0] read_addr = x_coord + 12'd1;
    wire        we        ; // Defined later based on delayed 'de'
    wire [11:0] wr_addr   ; // Defined later
    wire [26:0] wr_data   ; // Defined later

    always @(posedge clk) begin
        // Read Port (Top-Right Error for next clock)
        if (read_addr < 1024)
            bram_rd_data <= err_bram[read_addr];
        else
            bram_rd_data <= 27'd0;
            
        // Write Port (New Error from Combinational Math)
        if (we && wr_addr < 1024) begin
            err_bram[wr_addr] <= wr_data;
        end
    end

    // ==========================================
    // 2. Delay Pipeline
    // ==========================================
    reg [DATA_WIDTH-1:0] p_d1, p_d2;
    reg [11:0] x_d1, x_d2;
    reg [2:0] ctrl_d1, ctrl_d2; // {de, vs, hs}

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            p_d1 <= 0; p_d2 <= 0;
            x_d1 <= 0; x_d2 <= 0;
            ctrl_d1 <= 0; ctrl_d2 <= 0;
        end else begin
            p_d1 <= din;   p_d2 <= p_d1;
            x_d1 <= x_coord; x_d2 <= x_d1;
            ctrl_d1 <= {de_in, vs_in, hs_in};
            ctrl_d2 <= ctrl_d1;
        end
    end
    
    wire de_d2 = ctrl_d2[2];
    wire vs_d2 = ctrl_d2[1];
    wire hs_d2 = ctrl_d2[0];

    // ==========================================
    // 3. Error Shift Registers
    // ==========================================
    // Stage 1 output (bram_rd_data) gets captured into Stage 2 registers
    reg signed [8:0] err_tr_r, err_tr_g, err_tr_b;
    reg signed [8:0] err_t_r,  err_t_g,  err_t_b;
    reg signed [8:0] err_tl_r, err_tl_g, err_tl_b;
    
    // Left error is generated directly from math block
    reg signed [8:0] err_l_r, err_l_g, err_l_b;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            err_tr_r <= 0; err_tr_g <= 0; err_tr_b <= 0;
            err_t_r  <= 0; err_t_g  <= 0; err_t_b  <= 0;
            err_tl_r <= 0; err_tl_g <= 0; err_tl_b <= 0;
        end else begin
            // Shift operations happens along with Stage 2 processing
            err_tr_r <= bram_rd_data[26:18];
            err_tr_g <= bram_rd_data[17:9];
            err_tr_b <= bram_rd_data[8:0];
            
            err_t_r  <= err_tr_r;
            err_t_g  <= err_tr_g;
            err_t_b  <= err_tr_b;
            
            err_tl_r <= err_t_r;
            err_tl_g <= err_t_g;
            err_tl_b <= err_t_b;
        end
    end

    // ==========================================
    // 4. Combinational Math & Diffusion (Stage 2)
    // ==========================================
    // Boundary enforcement
    wire is_right_edge = (x_d2 == 12'd959);
    wire is_left_edge  = (x_d2 == 12'd0);

    wire signed [8:0] real_err_tr_r = is_right_edge ? 9'sd0 : err_tr_r;
    wire signed [8:0] real_err_tr_g = is_right_edge ? 9'sd0 : err_tr_g;
    wire signed [8:0] real_err_tr_b = is_right_edge ? 9'sd0 : err_tr_b;
    
    wire signed [8:0] real_err_tl_r = is_left_edge ? 9'sd0 : err_tl_r;
    wire signed [8:0] real_err_tl_g = is_left_edge ? 9'sd0 : err_tl_g;
    wire signed [8:0] real_err_tl_b = is_left_edge ? 9'sd0 : err_tl_b;
    
    wire signed [8:0] real_err_l_r = is_left_edge ? 9'sd0 : err_l_r;
    wire signed [8:0] real_err_l_g = is_left_edge ? 9'sd0 : err_l_g;
    wire signed [8:0] real_err_l_b = is_left_edge ? 9'sd0 : err_l_b;

    // Summing weights (7, 3, 5, 1) / 16
    wire signed [12:0] sum_r = (real_err_l_r * 13'sd7) + (real_err_tr_r * 13'sd3) + (err_t_r * 13'sd5) + (real_err_tl_r * 13'sd1);
    wire signed [12:0] sum_g = (real_err_l_g * 13'sd7) + (real_err_tr_g * 13'sd3) + (err_t_g * 13'sd5) + (real_err_tl_g * 13'sd1);
    wire signed [12:0] sum_b = (real_err_l_b * 13'sd7) + (real_err_tr_b * 13'sd3) + (err_t_b * 13'sd5) + (real_err_tl_b * 13'sd1);

    wire signed [8:0] diff_r = sum_r >>> 4;
    wire signed [8:0] diff_g = sum_g >>> 4;
    wire signed [8:0] diff_b = sum_b >>> 4;

    // Apply to current pixel
    wire signed [9:0] val_r = $signed({1'b0, p_d2[23:16]}) + diff_r;
    wire signed [9:0] val_g = $signed({1'b0, p_d2[15:8]})  + diff_g;
    wire signed [9:0] val_b = $signed({1'b0, p_d2[7:0]})   + diff_b;

    // Quantize and Error Calc
    reg [7:0] out_r, out_g, out_b;
    reg signed [8:0] new_err_r, new_err_g, new_err_b;

    always @(*) begin
        if (!error_diffusion_en) begin
            out_r = p_d2[23:16];
            out_g = p_d2[15:8];
            out_b = p_d2[7:0];
            new_err_r = 0; new_err_g = 0; new_err_b = 0;
        end else begin
            if (x_d2 < 12'd480) begin
                // Left Screen: No Error Diffusion (Clean Clipping)
                out_r = (p_d2[23:16] >= dither_threshold) ? p_d2[23:16] : 8'd0;
                out_g = (p_d2[15:8]  >= dither_threshold) ? p_d2[15:8]  : 8'd0;
                out_b = (p_d2[7:0]   >= dither_threshold) ? p_d2[7:0]   : 8'd0;
                new_err_r = 0; new_err_g = 0; new_err_b = 0;
            end else begin
                // Right Screen: Full Conditional Error Diffusion
                // R Channel
                if (p_d2[23:16] >= dither_threshold) begin
                     out_r = p_d2[23:16]; // Bypass Original
                end else if (val_r[9]) begin // Negative
                     out_r = 8'd0;
                end else if (val_r > 255) begin
                     out_r = 8'd240;
                end else begin
                     out_r = val_r[7:0] & 8'hF0; // Dithered & Truncated
                end
                new_err_r = val_r - $signed({2'b0, out_r});

                // G Channel
                if (p_d2[15:8] >= dither_threshold) begin
                     out_g = p_d2[15:8]; // Bypass Original
                end else if (val_g[9]) begin
                     out_g = 8'd0;
                end else if (val_g > 255) begin
                     out_g = 8'd240;
                end else begin
                     out_g = val_g[7:0] & 8'hF0;
                end
                new_err_g = val_g - $signed({2'b0, out_g});

                // B Channel
                if (p_d2[7:0] >= dither_threshold) begin
                     out_b = p_d2[7:0]; // Bypass Original
                end else if (val_b[9]) begin
                     out_b = 8'd0;
                end else if (val_b > 255) begin
                     out_b = 8'd240;
                end else begin
                     out_b = val_b[7:0] & 8'hF0;
                end
                new_err_b = val_b - $signed({2'b0, out_b});
            end
            
            // If we are not in active video, zero out the errors to prevent bleeding
            if (!de_d2) begin
                new_err_r = 0; new_err_g = 0; new_err_b = 0;
            end
        end
    end

    // Update the Left error register for the NEXT clock cycle
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            err_l_r <= 0; err_l_g <= 0; err_l_b <= 0;
        end else begin
            err_l_r <= new_err_r;
            err_l_g <= new_err_g;
            err_l_b <= new_err_b;
        end
    end

    // ==========================================
    // 5. Output Stage (Stage 3)
    // ==========================================
    assign we      = error_diffusion_en; // Always write over BRAM to clear/update it
    assign wr_addr = x_d2;
    assign wr_data = {new_err_r, new_err_g, new_err_b};

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            dout <= 0;
            hs_out <= 0; vs_out <= 0; de_out <= 0;
        end else begin
            dout   <= {out_r, out_g, out_b};
            hs_out <= hs_d2;
            vs_out <= vs_d2;
            de_out <= de_d2;
        end
    end

endmodule
