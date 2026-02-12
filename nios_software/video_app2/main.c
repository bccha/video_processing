#include "altera_avalon_jtag_uart_regs.h"
#include "altera_avalon_timer_regs.h"
#include "hdmi_config.h"
#include "io.h"
#include "nios2.h" // For NIOS2_WRITE_STATUS
#include "sys/alt_alarm.h"
#include "sys/alt_cache.h"
#include "sys/alt_irq.h" // For alt_irq_enable_all
#include "system.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// ============================================================================
// Configuration
// ============================================================================
#define OCM_TEST_WORDS 1024         // 4KB OCM-to-DDR
#define DDR_TEST_WORDS (256 * 1024) // 1MB DDR-to-DDR

// Nios II Data Cache Bypass Mask (Bit 31)
#define CACHE_BYPASS_MASK 0x80000000

// DDR3 Window Base Address
#define DDR3_WINDOW_BASE                                                       \
  (ADDRESS_SPAN_EXTENDER_0_WINDOWED_SLAVE_BASE | CACHE_BYPASS_MASK)

// CSR Register Offsets (Refer to burst_master_4.v)
#define REG_CTRL (0 * 4)
#define REG_STATUS (1 * 4)
#define REG_SRC_ADDR (2 * 4)
#define REG_DST_ADDR (3 * 4)
#define REG_LEN (4 * 4)
#define REG_RD_BURST (5 * 4)
#define REG_WR_BURST (6 * 4)
#define REG_COEFF (7 * 4)

// OCM Static Buffer
static unsigned int ocm_src_buffer[OCM_TEST_WORDS] __attribute__((aligned(32)));

// ----------------------------------------------------------------------------
// [Helper] Light-weight input functions for Small C Library
// ----------------------------------------------------------------------------

// Blocking version: Waits until a character is received
char get_char_polled() {
  unsigned int data;
  while (1) {
    data = IORD_ALTERA_AVALON_JTAG_UART_DATA(JTAG_UART_BASE);
    if (data & ALTERA_AVALON_JTAG_UART_DATA_RVALID_MSK) {
      return (char)(data & ALTERA_AVALON_JTAG_UART_DATA_DATA_MSK);
    }
  }
}

// Non-blocking (Async) version: Returns char if available, else returns 0
char get_char_async() {
  unsigned int data = IORD_ALTERA_AVALON_JTAG_UART_DATA(JTAG_UART_BASE);
  if (data & ALTERA_AVALON_JTAG_UART_DATA_RVALID_MSK) {
    return (char)(data & ALTERA_AVALON_JTAG_UART_DATA_DATA_MSK);
  }
  return 0; // No data available
}

// ----------------------------------------------------------------------------
// [Helper] High-resolution Timer (50MHz Snapshot + NTICKS)
// ----------------------------------------------------------------------------
// Returns current physical cycles (50MHz) since boot
unsigned long long get_total_cycles() {
  unsigned int t1, t2, snap;
  do {
    t1 = alt_nticks();
    IOWR_ALTERA_AVALON_TIMER_SNAPL(TIMER_0_BASE, 0);
    unsigned int low = IORD_ALTERA_AVALON_TIMER_SNAPL(TIMER_0_BASE);
    unsigned int high = IORD_ALTERA_AVALON_TIMER_SNAPH(TIMER_0_BASE);
    snap = (high << 16) | low;
    t2 = alt_nticks();
  } while (t1 != t2); // Ensure tick and snapshot are consistent

  // Timer counts down from 49999 to 0
  unsigned long long cycles = (unsigned long long)t1 * 50000;
  cycles += (49999 - snap);
  return cycles;
}

// ============================================================================
// [Function 1] OCM to DDR DMA Test (Original burst_master_0)
// ============================================================================
void run_ocm_to_ddr_test(unsigned int csr_base) {
  printf("\n--- [TEST 1] OCM to DDR DMA (burst_master_0) ---\n");

  unsigned int *src_ptr = ocm_src_buffer;
  unsigned int *dst_ptr =
      (unsigned int *)(DDR3_WINDOW_BASE); // DDR Offset 0 (Window 0)
  unsigned int src_phys = (unsigned int)src_ptr & 0x7FFFFFFF;

  // Clear destination to ensure fresh test
  for (int i = 0; i < OCM_TEST_WORDS; i++) {
    src_ptr[i] = i + 0x11110000;
    dst_ptr[i] = 0;
  }
  alt_dcache_flush_all();

  printf("Starting SW Copy (4KB x 100)... ");
  unsigned long long sw_t_start = get_total_cycles();
  for (int j = 0; j < 100; j++) {
    for (int i = 0; i < OCM_TEST_WORDS; i++) {
      dst_ptr[i] = src_ptr[i];
    }
  }
  unsigned long long sw_t_end = get_total_cycles();
  unsigned int sw_delta = (unsigned int)(sw_t_end - sw_t_start);
  if (sw_delta == 0)
    sw_delta = 1;
  // Use 10x scale for 0.1 MB/s precision
  unsigned int sw_rate_x10 =
      (unsigned int)((unsigned long long)OCM_TEST_WORDS * 4 * 100 *
                     500000000ULL / sw_delta / 1048576ULL);
  printf("Done (%u cycles, ~%u.%u MB/s)\n", sw_delta, sw_rate_x10 / 10,
         sw_rate_x10 % 10);

  alt_dcache_flush_all();

  printf("Starting HW DMA (4KB x 100)... ");
  unsigned long long hw_t_start = get_total_cycles();

  unsigned int ddr_phys_base = 0x20000000;
  for (int j = 0; j < 100; j++) {
    IOWR_32DIRECT(csr_base, REG_SRC_ADDR, src_phys);
    IOWR_32DIRECT(csr_base, REG_DST_ADDR, ddr_phys_base);
    IOWR_32DIRECT(csr_base, REG_LEN, OCM_TEST_WORDS * 4);
    IOWR_32DIRECT(csr_base, REG_RD_BURST, 32);
    IOWR_32DIRECT(csr_base, REG_WR_BURST, 32);
    IOWR_32DIRECT(csr_base, REG_CTRL, 1);

    while (!(IORD_32DIRECT(csr_base, REG_STATUS) & 1))
      ;
    IOWR_32DIRECT(csr_base, REG_STATUS, 1);
  }

  unsigned long long hw_t_end = get_total_cycles();
  unsigned int hw_delta = (unsigned int)(hw_t_end - hw_t_start);
  if (hw_delta == 0)
    hw_delta = 1;
  unsigned int hw_rate_x10 =
      (unsigned int)((unsigned long long)OCM_TEST_WORDS * 4 * 100 *
                     500000000ULL / hw_delta / 1048576ULL);
  printf("Done (%u cycles, ~%u.%u MB/s)\n", hw_delta, hw_rate_x10 / 10,
         hw_rate_x10 % 10);
  printf("Speedup: %u x\n", sw_delta / hw_delta);

  alt_dcache_flush_all();

  // Verify
  int errors = 0;
  for (int i = 0; i < OCM_TEST_WORDS; i++) {
    unsigned int actual = dst_ptr[i];
    unsigned int expected = i + 0x11110000;
    if (actual != expected) {
      if (errors < 5) {
        printf("  Error at idx %d: Exp=%08X, Got=%08X\n", i, expected, actual);
      }
      errors++;
    }
  }
  if (errors == 0)
    printf("SUCCESS: OCM to DDR Verified!\n");
  else
    printf("FAILURE: %d errors in OCM test.\n", errors);
}

// ============================================================================
// [Function 2] DDR to DDR DMA Test (New burst_master_1 / burst_master_4)
// ============================================================================
void run_ddr_to_ddr_test(unsigned int csr_base) {
  printf("\n--- [TEST 2] DDR to DDR DMA (Burst Master 4) ---\n");
  printf("Transfer Size: 1 MB\n");

  const unsigned int src_offset = 0x01000000;    // 16MB
  const unsigned int dst_sw_offset = 0x02000000; // 32MB
  const unsigned int dst_hw_offset = 0x03000000; // 48MB

  unsigned int *src_ptr = (unsigned int *)(DDR3_WINDOW_BASE + src_offset);
  unsigned int *dst_sw_ptr = (unsigned int *)(DDR3_WINDOW_BASE + dst_sw_offset);
  unsigned int *dst_hw_ptr = (unsigned int *)(DDR3_WINDOW_BASE + dst_hw_offset);

  // Init 1MB
  printf("Initializing DDR3 data... ");
  for (int i = 0; i < DDR_TEST_WORDS; i++) {
    src_ptr[i] = i + 1023;
    dst_sw_ptr[i] = 0;
    dst_hw_ptr[i] = 0;
  }
  alt_dcache_flush_all();
  printf("Done.\n");

  unsigned int test_coeff = 800; // Pipeline coefficient
  printf("Starting SW Copy (1MB)... ");
  unsigned long long sw_t_start = get_total_cycles();
  for (int i = 0; i < DDR_TEST_WORDS; i++) {
    dst_sw_ptr[i] =
        (unsigned int)((unsigned long long)src_ptr[i] * test_coeff / 400);
  }
  unsigned long long sw_t_end = get_total_cycles();
  unsigned int sw_delta = (unsigned int)(sw_t_end - sw_t_start);
  if (sw_delta == 0)
    sw_delta = 1;
  unsigned int sw_rate_x10 =
      (unsigned int)((unsigned long long)DDR_TEST_WORDS * 4 * 500000000ULL /
                     sw_delta / 1048576ULL);
  printf("Done (%u cycles, ~%u.%u MB/s)\n", sw_delta, sw_rate_x10 / 10,
         sw_rate_x10 % 10);

  alt_dcache_flush_all();

  // Configure for Max Burst (256)
  IOWR_32DIRECT(csr_base, REG_RD_BURST, 256);
  IOWR_32DIRECT(csr_base, REG_WR_BURST, 256);
  IOWR_32DIRECT(csr_base, REG_COEFF, test_coeff);

  printf("Starting HW DMA (1MB)... ");
  unsigned long long hw_t_start = get_total_cycles();

  unsigned int ddr_phys_base = 0x20000000; // 512MB
  IOWR_32DIRECT(csr_base, REG_SRC_ADDR, ddr_phys_base + src_offset);
  IOWR_32DIRECT(csr_base, REG_DST_ADDR, ddr_phys_base + dst_hw_offset);
  IOWR_32DIRECT(csr_base, REG_LEN, DDR_TEST_WORDS * 4);
  IOWR_32DIRECT(csr_base, REG_CTRL, 1);

  while (!(IORD_32DIRECT(csr_base, REG_STATUS) & 1))
    ;
  IOWR_32DIRECT(csr_base, REG_STATUS, 1);

  unsigned long long hw_t_end = get_total_cycles();
  unsigned int hw_delta = (unsigned int)(hw_t_end - hw_t_start);
  if (hw_delta == 0)
    hw_delta = 1;
  unsigned int hw_rate_x10 =
      (unsigned int)((unsigned long long)DDR_TEST_WORDS * 4 * 500000000ULL /
                     hw_delta / 1048576ULL);
  printf("Done (%u cycles, ~%u.%u MB/s)\n", hw_delta, hw_rate_x10 / 10,
         hw_rate_x10 % 10);
  printf("Speedup: %u x\n", sw_delta / hw_delta);

  printf("Verifying HW Output...\n");
  int errors = 0;
  for (int i = 0; i < 1024; i++) {
    unsigned int expected = dst_sw_ptr[i];
    unsigned int actual = dst_hw_ptr[i];
    int diff = (int)actual - (int)expected;
    if (diff > 1 || diff < -1)
      errors++;
  }
  if (errors == 0)
    printf("SUCCESS: DDR to DDR Verified! (Coeff=%u)\n", test_coeff);
  else
    printf("FAILURE: %d errors in DDR test.\n", errors);
}

// ============================================================================
// [Function 3] 720p Color Bar Pattern Generation
// ============================================================================
void generate_color_bar_pattern() {
  printf("\nGenerating 720p Color Bar Pattern in DDR3... ");
  unsigned int *fb = (unsigned int *)DDR3_WINDOW_BASE;
  const int width = 1280;
  const int height = 720;
  const int bar_width = width / 8; // 160 pixels per bar

  // Colors in XRGB8888 (32-bit)
  const unsigned int colors[8] = {
      0xFFFFFF, // White
      0xFFFF00, // Yellow
      0x00FFFF, // Cyan
      0x00FF00, // Green
      0xFF00FF, // Magenta
      0xFF0000, // Red
      0x0000FF, // Blue
      0x000000  // Black
  };

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

void print_menu() {
  printf("\n========== DE10-Nano HDMI Pipeline Menu ==========\n");
  printf(" [1] Perform OCM-to-DDR DMA Test (4KB)\n");
  printf(" [2] Perform DDR-to-DDR Burst Master Test (1MB)\n");
  printf(" [3] Initialize HDMI (ADV7513 via I2C)\n");
  printf(" [4] Generate 720p Color Bar Pattern in DDR3\n");
  printf(" [q] Quit\n");
  printf("--------------------------------------------------\n");
  printf("Select an option: ");
}

void run_interactive_menu() {
  char choice;
  while (1) {
    print_menu();

    // Clear stdin buffer and read one char
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
    case 'q':
      printf("Exiting... Goodbye!\n");
      return;
    default:
      printf("Invalid option! Please try again.\n");
      break;
    }
  }
}

// ============================================================================
// Main Entry
// ============================================================================
int main() {
  printf("\nDE10-Nano Video/DMA Test Environment Initialized\n");

  // Force Start Timer & Enable Global Interrupts (PIE bit)
  NIOS2_WRITE_STATUS(1);
  IOWR_ALTERA_AVALON_TIMER_CONTROL(TIMER_0_BASE,
                                   ALTERA_AVALON_TIMER_CONTROL_CONT_MSK |
                                       ALTERA_AVALON_TIMER_CONTROL_START_MSK |
                                       ALTERA_AVALON_TIMER_CONTROL_ITO_MSK);

  // Debug: Check if timer is moving
  printf("Checking Timer... ");
  unsigned long long start_time = get_total_cycles();
  for (volatile int i = 0; i < 10000; i++)
    ; // Busy wait
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

  // Start interactive session
  run_interactive_menu();

  return 0;
}
