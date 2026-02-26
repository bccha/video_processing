#ifndef HDMI_CONTROL_H_
#define HDMI_CONTROL_H_

#include "system.h"                       // Needed for HDMI_SYNC_BASE
#define HDMI_SYNC_GEN_BASE HDMI_SYNC_BASE // 0x2080 from system.h
#define REG_PATTERN_MODE (0 * 4)
#define REG_GLOBAL_CTRL (1 * 4)
#define REG_LUT_ADDR (2 * 4)
#define REG_LUT_DATA (3 * 4)
#define REG_BITMAP_ADDR (4 * 4)
#define REG_BITMAP_DATA (5 * 4)
#define REG_FRAME_PTR (6 * 4)
#define REG_FILTER_CONFIG (7 * 4)

// filter_config bit fields
#define FILTER_CFG_ERR_DIFF_EN (1 << 0) // bit[0]: error diffusion enable
#define FILTER_CFG_DEGAMMA_EN (1 << 1)  // bit[1]: de-gamma enable
#define FILTER_CFG_DITHER_EN (1 << 3)   // bit[3]: Global Dither Enable (Bayer)

// Color Matrix Avalon-MM Slave
// Base address from system.h (generated 2026-02-22): COLOR_MATRIX_BASE =
// 0x20200 Addr 0: control [0]=matrix_en, Addr 1-9: C00~C22 (12-bit signed,
// x1024)
#define COLOR_MATRIX_BASE 0x2000
#define CM_REG_CTRL (0 * 4)
#define CM_REG_C00 (1 * 4)
#define CM_REG_C01 (2 * 4)
#define CM_REG_C02 (3 * 4)
#define CM_REG_C10 (4 * 4)
#define CM_REG_C11 (5 * 4)
#define CM_REG_C12 (6 * 4)
#define CM_REG_C20 (7 * 4)
#define CM_REG_C21 (8 * 4)
#define CM_REG_C22 (9 * 4)

void generate_color_bar_pattern();
void run_dma_video_test();
void run_dma_control_submenu();
void change_rtl_pattern();
void run_gamma_submenu();
void load_gamma_table(float gamma_val);
void set_gamma_enable(int enable);
void load_char_bitmap();
void load_srgb_gamma_table();
void load_inverse_gamma_table();
void run_image_filter_submenu();
void run_color_calibration_submenu();

#endif /* HDMI_CONTROL_H_ */
