#include "hdmi_control.h"
#include "common.h"
#include <math.h>
#include <stdio.h>

void generate_color_bar_pattern() {
  printf("\nGenerating 720p Color Bar Pattern in DDR3... ");
  unsigned int *fb = (unsigned int *)DDR3_WINDOW_BASE;
  const int width = 1280;
  const int height = 720;
  const int bar_width = width / 8;

  const unsigned int colors[8] = {0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00,
                                  0xFF00FF, 0xFF0000, 0x0000FF, 0x000000};

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int color_idx = x / bar_width;
      if (color_idx > 7)
        color_idx = 7;
      fb[y * width + x] = colors[color_idx];
    }
  }

  alt_dcache_flush_all();
  printf("Done! (Total %d pixels written)\n", width * height);
}

void run_gamma_submenu() {
  static int gamma_en = 0;
  while (1) {
    printf("\n--- Gamma Correction Settings ---\n");
    printf(" [1] Toggle Enable (Current: %s)\n", gamma_en ? "ON" : "OFF");
    printf(" [2] Load Gamma 2.2 (Standard)\n");
    printf(" [3] Load sRGB Gamma (Standard)\n");
    printf(" [4] Load Inverse Gamma 2.2 (for Linear Panel)\n");
    printf(" [b] Back to Main Menu\n");
    printf("Enter choice: ");

    char c = get_char_polled();
    printf("%c\n", c);

    if (c == 'b')
      break;
    if (c == '1') {
      gamma_en = !gamma_en;
      set_gamma_enable(gamma_en);
    } else if (c == '2') {
      load_gamma_table(2.2f);
    } else if (c == '3') {
      load_srgb_gamma_table();
    } else if (c == '4') {
      load_inverse_gamma_table();
    }
  }
}

void change_rtl_pattern() {
  while (1) {
    printf("\nSelect RTL Pattern Mode:\n");
    printf(" [0] Solid Red\n");
    printf(" [1] Solid Green\n");
    printf(" [2] Solid Blue\n");
    printf(" [3] Grayscale Ramp\n");
    printf(" [4] Grid Pattern\n");
    printf(" [5] Solid White\n");
    printf(" [6] 8-level Gray Scale\n");
    printf(" [7] Character Tile (4x Scaling)\n");
    printf(" [b] Back to Main Menu\n");
    printf("Enter choice: ");

    char c = get_char_polled();
    printf("%c\n", c);

    if (c == 'b') {
      break;
    }

    unsigned int mode = c - '0';
    if (mode <= 7) {
      IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_PATTERN_MODE,
                    mode);
      printf("Pattern changed to %u\n", mode);
    } else {
      printf("Invalid mode! Try again or press 'b' to go back.\n");
    }
  }
}

void load_gamma_table(float gamma_val) {
  printf("Calculating and Loading Gamma Table (index^1/%.1f)... \n", gamma_val);
  float inv_gamma = 1.0f / gamma_val;

  for (int i = 0; i < 256; i++) {
    float normalized = (float)i / 255.0f;
    float corrected = powf(normalized, inv_gamma);
    unsigned char val = (unsigned char)(corrected * 255.0f + 0.5f);

    IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_LUT_ADDR, i);
    IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_LUT_DATA, val);

    // Print values (16 per line)
    printf("%3d ", val);
    if ((i + 1) % 16 == 0)
      printf("\n");
  }
  printf("Done.\n");
}

void set_gamma_enable(int enable) {
  IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_GAMMA_CTRL,
                enable ? 1 : 0);
  printf("Gamma Correction %s\n", enable ? "Enabled" : "Disabled");
}

void load_srgb_gamma_table() {
  printf("Calculating and Loading sRGB Gamma Table...\n");
  for (int i = 0; i < 256; i++) {
    float normalized = (float)i / 255.0f;
    float corrected;

    // sRGB Forward Transformation (Linear to sRGB space)
    if (normalized <= 0.0031308f) {
      corrected = 12.92f * normalized;
    } else {
      corrected = 1.055f * powf(normalized, 1.0f / 2.4f) - 0.055f;
    }

    unsigned char val = (unsigned char)(corrected * 255.0f + 0.5f);

    IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_LUT_ADDR, i);
    IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_LUT_DATA, val);

    printf("%3d ", val);
    if ((i + 1) % 16 == 0)
      printf("\n");
  }
  printf("sRGB Gamma Loaded.\n");
}

void load_inverse_gamma_table() {
  printf("Calculating and Loading Inverse Gamma Table (x^2.2) for Linear "
         "Panels...\n");
  for (int i = 0; i < 256; i++) {
    float normalized = (float)i / 255.0f;
    float corrected = powf(normalized, 2.2f);
    unsigned char val = (unsigned char)(corrected * 255.0f + 0.5f);

    IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_LUT_ADDR, i);
    IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_LUT_DATA, val);
  }
  printf("Inverse Gamma Loaded.\n");
}

void load_char_bitmap() {
  printf("Loading Custom Character Bitmap... ");
  /*
   * User Requested Custom Pattern (12x12 aligned to 16x16):
   * [Row 00]   ****   **    (0x3C60)
   * [Row 01]          **    (0x0060)
   * [Row 02]  ******  **    (0x7E60)
   * [Row 03]      **  **    (0x0660)
   * [Row 04]     ***  **    (0x0E60)
   * [Row 05]    ***   ***   (0x1C70)
   * [Row 06]   *****  **    (0x3E60)
   * [Row 07]  *** *** **    (0x7760)
   * [Row 08] ***   ** **    (0xE360)
   * [Row 09]          **    (0x0060)
   * [Row 10]          **    (0x0060)
   * [Row 11]          **    (0x0060)
   */
  unsigned short bitmap[16] = {0x3C60, 0x0060, 0x7E60, 0x0660, 0x0E60, 0x1C70,
                               0x3E60, 0x7760, 0xE360, 0x0060, 0x0060, 0x0060,
                               0x0000, 0x0000, 0x0000, 0x0000};

  for (int i = 0; i < 16; i++) {
    IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_BITMAP_ADDR, i);
    IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_BITMAP_DATA,
                  bitmap[i]);
  }
  printf("Done.\n");
}
