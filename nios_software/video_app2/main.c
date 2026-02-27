#include "burst_master_test.h"
#include "common.h"
#include "hdmi_config.h"
#include "hdmi_control.h"
#include "mem_verify.h"
#include "nios2.h"
#include <stdio.h>

void print_menu() {
  printf("\n========== DE10-Nano HDMI Pipeline Menu ==========\n");
  printf(" [1] Perform OCM-to-DDR DMA Test (4KB)\n");
  printf(" [2] Perform DDR-to-DDR Burst Master Test (1MB)\n");
  printf(" [3] Initialize HDMI (ADV7513 via I2C)\n");
  printf(" [4] Initialize 540p Color Bar in DDR3\n");
  printf(" [5] Change RTL Test Pattern (Red, Green, Blue, etc.)\n");
  printf(" [6] Gamma Correction Settings (Table, Toggle, Standard)\n");
  printf(" [7] Image Filter Control (Bypass, Blur, Edge, De-Gamma)\n");
  printf(" [8] DMA Video Stream Test (Mode 8)\n");
  printf(" [9] DMA Start/Stop Control Submenu\n");
  printf(" [G] Color Calibration (De-Gamma + 3x3 Gamut Matrix)\n");
  printf(" [C] Load Custom Character Bitmap\n");
  printf(" [r] Reset RTL Pattern Generator\n");
  printf(" [q] Test DDR Memory (TMEM_Verify)\n");
  printf("--------------------------------------------------\n");
  printf("Select an option: ");
}

void test_ddr_memory() {
  printf("\nHello from Nios II!\n");
  bool bPass;
  printf("HPS DDR3 Memory test code\n");

  int *p = ADDRESS_SPAN_EXTENDER_0_WINDOWED_SLAVE_BASE;
  *p = 0x00;
  printf("before %x\n", p);
  *p = 0xDEADBEEF;
    printf("after %x\n", p);
  // Qsys에서 이미 설정된 span을 이용하여 검증 (Cache Bypass 적용)
  bPass = TMEM_Verify(ADDRESS_SPAN_EXTENDER_0_WINDOWED_SLAVE_BASE |
                          CACHE_BYPASS_MASK,
                      ADDRESS_SPAN_EXTENDER_0_WINDOWED_SLAVE_SPAN, 0x01, 1);
  if (bPass)
    printf("HPS DDR3 test success\n");
  else
    printf("HPS DDR3 test failed\n");
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
    case '7':
      run_image_filter_submenu();
      break;
    case '8':
      run_dma_video_test();
      break;
    case '9':
      run_dma_control_submenu();
      break;
    case 'G':
    case 'g':
      run_color_calibration_submenu();
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
      test_ddr_memory();
      break;
    default:
      printf("Invalid option! Please try again.\n");
      break;
    }
  }
}

int main() {
  printf("\nDE10-Nano Video/DMA/ST2110 Test Environment Initialized\n");

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

  run_interactive_menu();
  return 0;
}
