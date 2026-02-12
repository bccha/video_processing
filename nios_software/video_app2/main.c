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
  printf(" [C] Load Custom Character Bitmap\n");
  printf(" [r] Reset RTL Pattern Generator\n");
  printf(" [q] Quit\n");
  printf("--------------------------------------------------\n");
  printf("Select an option: ");
}

void run_interactive_menu() {
  char choice;
  static int gamma_en = 0;
  while (1) {
    print_menu();
    choice = 0;
    while (choice < ' ') {
      choice = get_char_polled();
    }
    printf("%c\n", choice);

    switch (choice) {
    case '1':
      run_ocm_to_ddr_test(BURST_MASTER_0_BASE | CACHE_BYPASS_MASK);
      break;
    case '2':
#ifdef BURST_MASTER_4_0_BASE
      run_ddr_to_ddr_test(BURST_MASTER_4_0_BASE | CACHE_BYPASS_MASK);
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
  unsigned int ddr_phys_base = 0x20000000;
  printf("Initializing Span Extender to 0x%08X... ", ddr_phys_base);
  IOWR_32DIRECT(ADDRESS_SPAN_EXTENDER_0_CNTL_BASE, 0, ddr_phys_base);
  IOWR_32DIRECT(ADDRESS_SPAN_EXTENDER_0_CNTL_BASE, 4, 0);
  printf("Done.\n");
#endif

  run_interactive_menu();
  return 0;
}
