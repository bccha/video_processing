#include "io.h"
#include "sys/alt_alarm.h"
#include "sys/alt_cache.h"
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

// ============================================================================
// [Function 1] OCM to DDR DMA Test (Original burst_master_0)
// ============================================================================
void run_ocm_to_ddr_test(unsigned int csr_base) {
  printf("\n--- Test 1: OCM to DDR DMA (burst_master_0) ---\n");

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

  printf("Starting HW DMA (4KB)... ");
  alt_u32 start = alt_nticks();

  // Crucial: Move OCM test destination to 512MB offset to avoid ARM space
  unsigned int ddr_phys_base = 0x20000000;
  IOWR_32DIRECT(csr_base, REG_SRC_ADDR, src_phys);
  IOWR_32DIRECT(csr_base, REG_DST_ADDR, ddr_phys_base); // DDR Physical 512MB
  IOWR_32DIRECT(csr_base, REG_LEN, OCM_TEST_WORDS * 4);
  IOWR_32DIRECT(csr_base, REG_RD_BURST, 32);
  IOWR_32DIRECT(csr_base, REG_WR_BURST, 32);
  IOWR_32DIRECT(csr_base, REG_CTRL, 1);

  while (!(IORD_32DIRECT(csr_base, REG_STATUS) & 1))
    ;
  IOWR_32DIRECT(csr_base, REG_STATUS, 1);

  alt_u32 end = alt_nticks();
  printf("Done in %lu ticks.\n", (unsigned long)(end - start));

  // Invalidate cache before reading back from DDR if not bypassing
  // Though we use CACHE_BYPASS_MASK, full verification is better.
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
    printf("SUCCESS: OCM to DDR Verified! 🎉\n");
  else
    printf("FAILURE: %d errors in OCM test. ❌\n", errors);
}

// ============================================================================
// [Function 2] DDR to DDR DMA Test (New burst_master_1 / burst_master_4)
// ============================================================================
void run_ddr_to_ddr_test(unsigned int csr_base) {
  printf("\n--- Test 2: DDR to DDR DMA (burst_master_1 / 4-Stage Pipe) ---\n");
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

  // --- Step 2-A: Software Copy Performance (Including Pipeline Math) ---
  unsigned int test_coeff = 800; // Pipeline coefficient
  printf("Starting SW Copy (1MB, Coeff=%d)... ", test_coeff);
  alt_u32 sw_start = alt_nticks();
  for (int i = 0; i < DDR_TEST_WORDS; i++) {
    // Pure Software Approach: Standard Division (No HW optimization)
    dst_sw_ptr[i] =
        (unsigned int)((unsigned long long)src_ptr[i] * test_coeff / 400);
  }
  alt_u32 sw_end = alt_nticks();
  float sw_time_s = (float)(sw_end - sw_start) / alt_ticks_per_second();
  float sw_rate = (float)(DDR_TEST_WORDS * 4) / (1024.0 * 1024.0) / sw_time_s;
  printf("Done.\n  -> SW Time: %.3f s, Rate: %.2f MB/s\n", sw_time_s, sw_rate);

  alt_dcache_flush_all();

  // --- Step 2-B: Hardware DMA Performance ---
  // Configure for Max Burst (256) and Coefficient
  IOWR_32DIRECT(csr_base, REG_RD_BURST, 256);
  IOWR_32DIRECT(csr_base, REG_WR_BURST, 256);
  IOWR_32DIRECT(csr_base, REG_COEFF, test_coeff);

  printf("Starting HW DMA (1MB, Coeff=%d)... ", test_coeff);
  alt_u32 hw_start = alt_nticks();

  // Crucial: Use absolute physical address for DMA Master (DDR_BASE_PHYS +
  // offset)
  unsigned int ddr_phys_base = 0x20000000; // 512MB
  IOWR_32DIRECT(csr_base, REG_SRC_ADDR, ddr_phys_base + src_offset);
  IOWR_32DIRECT(csr_base, REG_DST_ADDR, ddr_phys_base + dst_hw_offset);
  IOWR_32DIRECT(csr_base, REG_LEN, DDR_TEST_WORDS * 4);
  IOWR_32DIRECT(csr_base, REG_CTRL, 1);

  while (!(IORD_32DIRECT(csr_base, REG_STATUS) & 1))
    ;
  IOWR_32DIRECT(csr_base, REG_STATUS, 1);

  alt_u32 hw_end = alt_nticks();
  float hw_time_s = (float)(hw_end - hw_start) / alt_ticks_per_second();
  float hw_rate = (float)(DDR_TEST_WORDS * 4) / (1024.0 * 1024.0) / hw_time_s;
  printf("Done.\n  -> HW Time: %.3f s, Rate: %.2f MB/s\n", hw_time_s, hw_rate);
  printf("  -> Speedup: %.2fx\n", hw_rate / sw_rate);

  // --- Step 3: Verification (Direct SW vs HW comparison) ---
  printf("Verifying Results (SW Output vs HW Output)...\n");
  int errors = 0;
  for (int i = 0; i < 1024; i++) { // Verify first 1K words
    unsigned int expected = dst_sw_ptr[i];
    unsigned int actual = dst_hw_ptr[i];
    int diff = (int)actual - (int)expected;

    // Allow tolerance of +/- 1 due to fixed-point vs division rounding
    if (diff > 1 || diff < -1) {
      if (errors < 5) {
        printf("  Error at idx %d: SW_Exp=%u, HW_Got=%u, Diff=%d\n", i,
               expected, actual, diff);
      }
      errors++;
    }
  }

  if (errors == 0)
    printf("SUCCESS: HW DMA results match SW reference! 🎉\n");
  else
    printf("FAILURE: %d mismatches found between SW and HW. ❌\n", errors);
}

// ============================================================================
// Main Entry
// ============================================================================
int main() {
  printf("\nNios II Dual Master DMA Benchmark System\n");

  // --- Crucial Step: Initialize Address Span Extender Window ---
  // The Span Extender maps Nios II's 128MB window to the HPS AXI Bridge.
  // We point it to 0x20000000 (512MB) to avoid ARM/Linux kernel space.
#ifdef ADDRESS_SPAN_EXTENDER_0_CNTL_BASE
  unsigned int ddr_phys_base = 0x20000000; // 512MB Offset
  printf("Setting Span Extender window to 0x%08X... ", ddr_phys_base);
  IOWR_32DIRECT(ADDRESS_SPAN_EXTENDER_0_CNTL_BASE, 0, ddr_phys_base);
  IOWR_32DIRECT(ADDRESS_SPAN_EXTENDER_0_CNTL_BASE, 4,
                0x00000000); // High 32-bit
  printf("Done.\n");
#endif

  // Run OCM test using Master 0
  run_ocm_to_ddr_test(BURST_MASTER_0_BASE | CACHE_BYPASS_MASK);

  // Run DDR test using burst_master_4 (if exists)
#ifdef BURST_MASTER_4_0_BASE
  run_ddr_to_ddr_test(BURST_MASTER_4_0_BASE | CACHE_BYPASS_MASK);
#else
  printf("\n[Warning] BURST_MASTER_4_0_BASE not found. Skipping DDR-to-DDR "
         "test.\n");
#endif

  printf("\nAll DMA Tests Finished.\n");
  return 0;
}
