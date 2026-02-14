#ifndef HDMI_CONTROL_H_
#define HDMI_CONTROL_H_

#define HDMI_SYNC_GEN_BASE 0x20020
#define REG_PATTERN_MODE (0 * 4)
#define REG_DMA_CTRL (1 * 4) // [31]Busy, [30]Done, [2]Start, [1]Cont, [0]Gamma
#define REG_LUT_ADDR (2 * 4)
#define REG_LUT_DATA (3 * 4)
#define REG_BITMAP_ADDR (4 * 4)
#define REG_BITMAP_DATA (5 * 4)
#define REG_FRAME_PTR (6 * 4)

// DMA Control Bit Masks
#define AS_DMA_BUSY_MSK (1 << 31)
#define AS_DMA_DONE_MSK (1 << 30)
#define AS_DMA_START_MSK (1 << 2)
#define AS_DMA_CONT_MSK (1 << 1)
#define AS_GAMMA_EN_MSK (1 << 0)

void generate_color_bar_pattern();
void change_rtl_pattern();
void run_gamma_submenu();
void load_gamma_table(float gamma_val);
void set_gamma_enable(int enable);
void load_char_bitmap();
void load_srgb_gamma_table();
void load_inverse_gamma_table();

// New DMA Control Functions
void dma_start_single();
void dma_set_continuous(int enable);
void print_dma_status();
void run_dma_debug_submenu();

#endif /* HDMI_CONTROL_H_ */
