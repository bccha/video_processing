#include "burst_master_test.h"
#include "common.h"
#include "hdmi_config.h"
#include "hdmi_control.h"
#include "nios2.h"
#include <stdio.h>

void print_menu() {
  printf("\n========== DE10-Nano HDMI Pipeline Menu ==========\n");
  printf(" [1] Perform OCM-to-DDR DMA Test (4KB)\n");
  printf(" [2] Perform DDR-to-DDR Burst Master Test (1MB)\n");
  printf(" [3] Initialize HDMI (ADV7513 via I2C)\n");
  printf(" [4] Generate 720p Color Bar Pattern in DDR3\n");
  printf(" [5] Change RTL Test Pattern (Red, Green, Blue, etc.)\n");
  printf(" [6] Gamma Correction Settings (Table, Toggle, Standard)\n");
  printf(" [8] DMA & Video Source Debug Submenu\n");
  printf(" [C] Load Custom Character Bitmap\n");
  printf(" [r] Reset RTL Pattern Generator\n");
  printf(" [q] Quit\n");
  printf("--------------------------------------------------\n");
  printf("Select an option: ");
}

void run_interactive_menu() {
  char choice;
  while (1) {
    print_menu();
    choice = 0;
    while (choice < ' ') {
      choice = get_char_polled();
    }
    printf("%c\n", choice);

    switch (choice) {
    case '1':
      // Switch Window to 0x20000000 for Benchmark
      IOWR_32DIRECT(ADDRESS_SPAN_EXTENDER_0_CNTL_BASE, 0, 0x20000000);
      printf("[Switch] Window mapped to 0x20000000 for Benchmark\n");

      run_ocm_to_ddr_test(BURST_MASTER_0_BASE | CACHE_BYPASS_MASK, 0x20000000);

      // Restore Window to 0x30000000 for Video
      IOWR_32DIRECT(ADDRESS_SPAN_EXTENDER_0_CNTL_BASE, 0, 0x30000000);
      printf("[Restore] Window mapped to 0x30000000 for Video\n");
      break;
    case '2':
#ifdef BURST_MASTER_4_0_BASE
      // Switch Window to 0x20000000 for Benchmark
      IOWR_32DIRECT(ADDRESS_SPAN_EXTENDER_0_CNTL_BASE, 0, 0x20000000);
      printf("[Switch] Window mapped to 0x20000000 for Benchmark\n");

      run_ddr_to_ddr_test(BURST_MASTER_4_0_BASE | CACHE_BYPASS_MASK,
                          0x20000000);

      // Restore Window to 0x30000000 for Video
      IOWR_32DIRECT(ADDRESS_SPAN_EXTENDER_0_CNTL_BASE, 0, 0x30000000);
      printf("[Restore] Window mapped to 0x30000000 for Video\n");
#else
      printf("Error: BURST_MASTER_4_0 not found in system.h\n");
#endif
      break;
    case '3':
      hdmi_init();
      break;
    case '4':
      generate_color_bar_pattern();
      break;
    case '5':
      change_rtl_pattern();
      break;
    case '8':
      run_dma_debug_submenu();
      break;
    case '6':
      run_gamma_submenu();
      break;
    case 'C':
    case 'c':
      load_char_bitmap();
      break;
    case 'r':
      IOWR_32DIRECT(HDMI_SYNC_GEN_BASE | CACHE_BYPASS_MASK, REG_PATTERN_MODE,
                    0);
      printf("RTL Pattern Reset to 0 (Red)\n");
      break;
    case 'q':
      printf("Exiting... Goodbye!\n");
      return;
    default:
      printf("Invalid option! Please try again.\n");
      break;
    }
  }
}

int main() {
  printf("\nDE10-Nano Video/DMA Test Environment Initialized\n");

  NIOS2_WRITE_STATUS(1);
  IOWR_ALTERA_AVALON_TIMER_CONTROL(TIMER_0_BASE,
                                   ALTERA_AVALON_TIMER_CONTROL_CONT_MSK |
                                       ALTERA_AVALON_TIMER_CONTROL_START_MSK |
                                       ALTERA_AVALON_TIMER_CONTROL_ITO_MSK);

  printf("Checking Timer... ");
  unsigned long long start_time = get_total_cycles();
  for (volatile int i = 0; i < 10000; i++)
    ;
  unsigned long long end_time = get_total_cycles();
  if (end_time > start_time) {
    printf("Timer OK! (Delta=%u)\n", (unsigned int)(end_time - start_time));
  } else {
    printf("Timer STUCK! (Val=%u)\n", (unsigned int)start_time);
  }

#ifdef ADDRESS_SPAN_EXTENDER_0_CNTL_BASE
  // HW default DMA address is 0x30000000.
  // Window Size is 128MB.
  // So we map the window start to 0x30000000 directly.
  unsigned int ddr_phys_base = 0x30000000;
  printf("Initializing Span Extender to 0x%08X... ", ddr_phys_base);
  IOWR_32DIRECT(ADDRESS_SPAN_EXTENDER_0_CNTL_BASE, 0, ddr_phys_base);
  IOWR_32DIRECT(ADDRESS_SPAN_EXTENDER_0_CNTL_BASE, 4, 0);
  printf("Done.\n");
#endif

#ifdef PLL_LOCKED_BASE
  printf("Checking PLL Lock Status... ");
  unsigned int pll_locked = IORD_32DIRECT(PLL_LOCKED_BASE, 0);
  if (pll_locked & 1) {
    printf("LOCKED (0x%x)\n", pll_locked);
  } else {
    printf("FAILED (0x%x)\n", pll_locked);
    printf("WARNING: HDMI Clock might be dead!\n");
  }
#endif

  run_interactive_menu();
  return 0;
}
