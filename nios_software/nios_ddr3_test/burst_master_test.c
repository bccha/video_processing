#include "burst_master_test.h"
#include "common.h"
#include <stdio.h>

static unsigned int ocm_src_buffer[OCM_TEST_WORDS] __attribute__((aligned(32)));

void run_ocm_to_ddr_test(unsigned int csr_base) {
  printf("\n--- [TEST 1] OCM to DDR DMA (burst_master_0) ---\n");

  unsigned int *src_ptr = ocm_src_buffer;
  unsigned int *dst_ptr = (unsigned int *)(DDR3_WINDOW_BASE);
  unsigned int src_phys = (unsigned int)src_ptr & 0x7FFFFFFF;

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

  int errors = 0;
  for (int i = 0; i < OCM_TEST_WORDS; i++) {
    if (dst_ptr[i] != (i + 0x11110000))
      errors++;
  }
  if (errors == 0)
    printf("SUCCESS: OCM to DDR Verified!\n");
  else
    printf("FAILURE: %d errors in OCM test.\n", errors);
}

void run_ddr_to_ddr_test(unsigned int csr_base) {
  printf("\n--- [TEST 2] DDR to DDR DMA (Burst Master 4) ---\n");
  printf("Transfer Size: 1 MB\n");

  const unsigned int src_offset = 0x01000000;
  const unsigned int dst_sw_offset = 0x02000000;
  const unsigned int dst_hw_offset = 0x03000000;

  unsigned int *src_ptr = (unsigned int *)(DDR3_WINDOW_BASE + src_offset);
  unsigned int *dst_sw_ptr = (unsigned int *)(DDR3_WINDOW_BASE + dst_sw_offset);
  unsigned int *dst_hw_ptr = (unsigned int *)(DDR3_WINDOW_BASE + dst_hw_offset);

  for (int i = 0; i < DDR_TEST_WORDS; i++) {
    src_ptr[i] = i + 1023;
    dst_sw_ptr[i] = 0;
    dst_hw_ptr[i] = 0;
  }
  alt_dcache_flush_all();

  unsigned int test_coeff = 800;
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

  IOWR_32DIRECT(csr_base, REG_RD_BURST, 256);
  IOWR_32DIRECT(csr_base, REG_WR_BURST, 256);
  IOWR_32DIRECT(csr_base, REG_COEFF, test_coeff);

  printf("Starting HW DMA (1MB)... ");
  unsigned long long hw_t_start = get_total_cycles();
  unsigned int ddr_phys_base = 0x20000000;
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
    int diff = (int)dst_hw_ptr[i] - (int)dst_sw_ptr[i];
    if (diff > 1 || diff < -1)
      errors++;
  }
  if (errors == 0)
    printf("SUCCESS: DDR to DDR Verified! (Coeff=%u)\n", test_coeff);
  else
    printf("FAILURE: %d errors in DDR test.\n", errors);
}
