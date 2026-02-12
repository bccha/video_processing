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
    printf(" [b] Back to Main Menu\n");
    printf("Enter choice: ");

    char c = get_char_polled();
    printf("%c\n", c);

    if (c == 'b') {
      break;
    }

    unsigned int mode = c - '0';
    if (mode <= 6) {
      IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_PATTERN_MODE,
                    mode);
      printf("Pattern changed to %u\n", mode);
    } else {
      printf("Invalid mode! Try again or press 'b' to go back.\n");
    }
  }
}

void load_gamma_table(float gamma_val) {
  printf("Calculating and Loading Gamma Table (index^1/%.1f)... ", gamma_val);
  float inv_gamma = 1.0f / gamma_val;

  for (int i = 0; i < 256; i++) {
    float normalized = (float)i / 255.0f;
    float corrected = powf(normalized, inv_gamma);
    unsigned char val = (unsigned char)(corrected * 255.0f + 0.5f);

    IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_LUT_ADDR, i);
    IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_LUT_DATA, val);
  }
  printf("Done.\n");
}

void set_gamma_enable(int enable) {
  IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_GAMMA_CTRL,
                enable ? 1 : 0);
  printf("Gamma Correction %s\n", enable ? "Enabled" : "Disabled");
}
