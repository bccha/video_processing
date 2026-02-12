#ifndef HDMI_CONTROL_H_
#define HDMI_CONTROL_H_

#define HDMI_SYNC_GEN_BASE 0x20020
#define REG_PATTERN_MODE (0 * 4)
#define REG_GAMMA_CTRL (1 * 4)
#define REG_LUT_ADDR (2 * 4)
#define REG_LUT_DATA (3 * 4)
#define REG_BITMAP_ADDR (4 * 4)
#define REG_BITMAP_DATA (5 * 4)

void generate_color_bar_pattern();
void change_rtl_pattern();
void run_gamma_submenu();
void load_gamma_table(float gamma_val);
void set_gamma_enable(int enable);
void load_char_bitmap();
void load_srgb_gamma_table();
void load_inverse_gamma_table();

#endif /* HDMI_CONTROL_H_ */
