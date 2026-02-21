#include "hdmi_control.h"
#include "common.h"
#include <io.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

void generate_color_bar_pattern() {
  printf("\nGenerating 540p (960x540) Color Bar Pattern in DDR3... ");
  unsigned int *fb = (unsigned int *)DDR3_WINDOW_BASE;
  const int width = 960;
  const int height = 540;
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

void run_dma_control_submenu() {
  while (1) {
    unsigned int ctrl =
        IORD_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_GLOBAL_CTRL);
    int busy = (ctrl >> 31) & 0x01;
    int done = (ctrl >> 30) & 0x01;
    int cont = (ctrl >> 1) & 0x01;

    printf("\n--- HDMI DMA Control Submenu ---\n");
    printf(" Status: %s | Continuous: %s | Done: %d\n", busy ? "BUSY" : "IDLE",
           cont ? "ON" : "OFF", done);
    printf(" [1] DMA Start (Pulse)\n");
    printf(" [2] Toggle Continuous Mode (%s -> %s)\n", cont ? "ON" : "OFF",
           cont ? "OFF" : "ON");
    printf(" [3] DMA STOP (Disable Continuous)\n");
    printf(" [4] Clear Done Flag\n");
    printf(" [5] Set Frame Pointer Address\n");
    printf(" [b] Back to Main Menu\n");
    printf("Enter choice: ");

    char c = get_char_polled();
    printf("%c\n", c);

    if (c == 'b')
      break;

    if (c == '1') {
      // Set Start Bit (Bit 2)
      IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_GLOBAL_CTRL,
                    ctrl | 0x04);
      printf("DMA Start Pulse sent.\n");
    } else if (c == '2') {
      // Toggle Cont Bit (Bit 1)
      unsigned int next_ctrl = ctrl ^ 0x02;
      IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_GLOBAL_CTRL,
                    next_ctrl);
      printf("Continuous Mode Toggled.\n");
    } else if (c == '3') {
      // Clear Cont Bit (Bit 1)
      IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_GLOBAL_CTRL,
                    ctrl & ~0x02);
      printf("DMA Stopped (Continuous Disable).\n");
    } else if (c == '4') {
      // Clear Done (Write 1 to bit 30)
      IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_GLOBAL_CTRL,
                    ctrl | 0x40000000);
      printf("Done flag cleared.\n");
    } else if (c == '5') {
      printf("Enter FB Physical Address (Hex, e.g., 20000000): ");
      char addr_str[16];
      /*
      get_string_polled(addr_str, sizeof(addr_str));
      unsigned int new_addr = (unsigned int)strtoul(addr_str, NULL, 16);
      IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_FRAME_PTR,
                    new_addr);
      printf("Frame Pointer updated to 0x%08X\n", new_addr);
      */
    }
  }
}

void run_dma_video_test() {
  printf("\n--- Starting DMA Video Stream Test (960x540) ---\n");

  // 1. Frame Buffer should be initialized via Option [4]
  printf("Note: Ensure color bars are initialized via Option [4] first.\n");

  // 2. Set Frame Pointer in RTL (DDR3 Physical Address 0x20000000)
  unsigned int phys_addr = 0x20000000;
  printf("Setting Frame Pointer to 0x%08X... ", phys_addr);
  IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_FRAME_PTR,
                phys_addr);
  printf("Done.\n");

  // 3. Enable Continuous DMA (Bit 1 of Global Control)
  printf("Enabling DMA Streaming Mode... ");
  IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_GLOBAL_CTRL, 0x02);
  printf("Done.\n");

  // 4. Switch Sync Gen to Mode 8 (DMA Stream)
  printf("Switching Sync Gen to Mode 8 (DMA)... ");
  IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_PATTERN_MODE, 8);
  printf("Done.\n");

  printf("\nMonitoring DMA Status (Press any key to stop)...\n");
  printf("CTRL_REG: [Busy][Done]...[Cont][Gamma]\n");

  while (get_char_async() == 0) {
    unsigned int status =
        IORD_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_GLOBAL_CTRL);
    unsigned int mode =
        IORD_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_PATTERN_MODE);

    int busy = (status >> 31) & 0x01;
    int done = (status >> 30) & 0x01;

    printf("\rStatus: 0x%08X | Busy: %d | Done: %d | Mode: %u ", status, busy,
           done, mode);

    // Minimal delay
    for (volatile int i = 0; i < 50000; i++)
      ;
  }
  printf("\nMonitoring stopped.\n");
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
  IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_GLOBAL_CTRL,
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

void run_image_filter_submenu() {
  while (1) {
    uint32_t current_mode =
        IORD_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_PATTERN_MODE);
    int temporal_en = (current_mode >> 8) & 0x01;
    int dither_2bit_en = (current_mode >> 9) & 0x01;
    uint32_t filter_cfg = IORD_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK,
                                        REG_FILTER_CONFIG);
    int err_diff_en = filter_cfg & 0x01;
    int err_diff_th = (filter_cfg >> 8) & 0xFF;

    printf("\n--- Image Filter Control ---\n");
    printf(" [0] Bypass (Full Color)\n");
    printf(" [1] Grayscale\n");
    printf(" [2] Blur (Grayscale)\n");
    printf(" [3] Blur (Color)\n");
    printf(" [4] Edge (Grayscale)\n");
    printf(" [5] Edge (Color)\n");
    printf(" [6] Emboss (Grayscale)\n");
    printf(" [7] Sharpen (Color)\n");
    printf(" [8] Bayer Dithering (Split Screen)\n");
    printf(" [9] Toggle Temporal Dithering (Current: %s)\n",
           temporal_en ? "ON" : "OFF");
    printf(" [a] Toggle 2-Bit Dither Mode (Current: %s)\n",
           dither_2bit_en ? "2-Bit" : "4-Bit");
    printf(" [c] Toggle Error Diffusion (Current: %s)\n",
           err_diff_en ? "ON" : "OFF");
    printf(" [t] Set Dither Threshold (Current: 0x%02X)\n", err_diff_th);
    printf(" [b] Return to main menu\n");
    printf("Select filter mode: ");

    char choice = 0;
    while (choice < ' ') {
      choice = get_char_polled();
    }
    printf("%c\n", choice);

    if (choice >= '0' && choice <= '8') {
      uint32_t filter_val = choice - '0';
      // Read current mode, clear bits [7:4], set new mode, and write back
      current_mode = IORD_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK,
                                   REG_PATTERN_MODE);
      current_mode = (current_mode & ~(0xF << 4)) | (filter_val << 4);
      IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_PATTERN_MODE,
                    current_mode);
      printf(">> Filter Mode set to %d\n", filter_val);
    } else if (choice == '9') {
      current_mode = IORD_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK,
                                   REG_PATTERN_MODE);
      current_mode ^= (1 << 8); // Toggle bit 8
      IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_PATTERN_MODE,
                    current_mode);
      printf(">> Temporal Dithering %s\n",
             (current_mode & (1 << 8)) ? "Enabled" : "Disabled");
    } else if (choice == 'a' || choice == 'A') {
      current_mode = IORD_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK,
                                   REG_PATTERN_MODE);
      current_mode ^= (1 << 9); // Toggle bit 9
      IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_PATTERN_MODE,
                    current_mode);
      printf(">> Dither Mode set to %s\n",
             (current_mode & (1 << 9)) ? "2-Bit" : "4-Bit");
    } else if (choice == 'c' || choice == 'C') {
      filter_cfg ^= 0x01;
      IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_FILTER_CONFIG,
                    filter_cfg);
      printf(">> Error Diffusion %s\n",
             (filter_cfg & 0x01) ? "Enabled" : "Disabled");
    } else if (choice == 't' || choice == 'T') {
      printf("Enter threshold (Hex 00-FF): ");
      char c1 = get_char_polled();
      printf("%c", c1);
      char c2 = get_char_polled();
      printf("%c\n", c2);
      int val = 0;
      if (c1 >= '0' && c1 <= '9')
        val += (c1 - '0') * 16;
      else if (c1 >= 'a' && c1 <= 'f')
        val += (c1 - 'a' + 10) * 16;
      else if (c1 >= 'A' && c1 <= 'F')
        val += (c1 - 'A' + 10) * 16;

      if (c2 >= '0' && c2 <= '9')
        val += (c2 - '0');
      else if (c2 >= 'a' && c2 <= 'f')
        val += (c2 - 'a' + 10);
      else if (c2 >= 'A' && c2 <= 'F')
        val += (c2 - 'A' + 10);

      filter_cfg = (filter_cfg & ~(0xFF << 8)) | (val << 8);
      IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_FILTER_CONFIG,
                    filter_cfg);
      printf(">> Threshold set to 0x%02X\n", val);
    } else if (choice == 'b' || choice == 'B') {
      return;
    } else {
      printf(">> Invalid selection.\n");
    }
  }
}
