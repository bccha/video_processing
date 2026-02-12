#include "alt_types.h"
#include "system.h"
#include <io.h>
#include <stdio.h>
#include <sys/alt_alarm.h> // For alt_nticks
#include <sys/alt_cache.h>
#include <unistd.h>


// CSR Register Map for burst_master
#define BURST_CTRL_REG (0 * 4)
#define BURST_STATUS_REG (1 * 4)
#define BURST_SRC_ADDR_REG (2 * 4)
#define BURST_DST_ADDR_REG (3 * 4)
#define BURST_LEN_REG (4 * 4)
#define BURST_RD_CNT_REG (5 * 4)
#define BURST_WR_CNT_REG (6 * 4)

// Reduced size to fit in On-Chip Memory alongside program code
#define TEST_WORDS 1024 // 4KB
#define BYTES_TO_COPY (TEST_WORDS * 4)
#define LOOP_COUNT 100 // Repeat 100 times for better timing

static unsigned int src_buffer[TEST_WORDS] __attribute__((aligned(32)));

int main() {
  unsigned int *src_ptr = src_buffer;
  unsigned int *dst_ptr =
      (unsigned int *)(ADDRESS_SPAN_EXTENDER_0_WINDOWED_SLAVE_BASE |
                       0x80000000);
  unsigned int csr_base = BURST_MASTER_0_BASE | 0x80000000;

  alt_u32 start_time, end_time;
  float sw_time_ms, hw_time_ms;
  int i, loop;

  printf("\n--- Burst Master Performance Test (100 Iterations) ---\n");
  printf("Unit Size: %d KB\n", BYTES_TO_COPY / 1024);
  printf("Total Size: %d KB (%d iterations)\n",
         (BYTES_TO_COPY * LOOP_COUNT) / 1024, LOOP_COUNT);
  fflush(stdout);

  // 1. Prepare Source Data
  for (i = 0; i < TEST_WORDS; i++) {
    src_ptr[i] = i + 0xEEEE0000;
  }
  alt_dcache_flush_all();

  // --- Software Performance Test ---
  printf("Step 1: Running Software Copy (100x)...\n");
  fflush(stdout);

  start_time = alt_nticks();
  for (loop = 0; loop < LOOP_COUNT; loop++) {
    for (i = 0; i < TEST_WORDS; i++) {
      IOWR_32DIRECT(dst_ptr, i * 4, src_ptr[i]);
    }
  }
  end_time = alt_nticks();

  sw_time_ms = (float)(end_time - start_time);
  printf("  -> SW Time: %.2f ms (Throughput: %.2f MB/s)\n", sw_time_ms,
         (float)(BYTES_TO_COPY * LOOP_COUNT) / (sw_time_ms * 1000.0));

  // Clear destination for clean test
  for (i = 0; i < TEST_WORDS; i++)
    IOWR_32DIRECT(dst_ptr, i * 4, 0);

  // --- Hardware Performance Test ---
  printf("Step 2: Running Hardware DMA (100x)...\n");
  fflush(stdout);

  unsigned int src_phys_addr = (unsigned int)src_ptr & 0x7FFFFFFF;

  IOWR_32DIRECT(csr_base, BURST_SRC_ADDR_REG, src_phys_addr);
  IOWR_32DIRECT(csr_base, BURST_DST_ADDR_REG, 0x20000000);
  IOWR_32DIRECT(csr_base, BURST_LEN_REG, BYTES_TO_COPY);
  IOWR_32DIRECT(csr_base, BURST_RD_CNT_REG, 32);
  IOWR_32DIRECT(csr_base, BURST_WR_CNT_REG, 32);

  start_time = alt_nticks();
  for (loop = 0; loop < LOOP_COUNT; loop++) {
    IOWR_32DIRECT(csr_base, BURST_CTRL_REG, 1); // Start!

    int timeout = 5000000;
    while (!(IORD_32DIRECT(csr_base, BURST_STATUS_REG) & 1) && timeout > 0) {
      timeout--;
    }

    if (timeout <= 0) {
      printf("  -> HW FAILED at iteration %d (Timeout)! 😭\n", loop);
      break;
    }
    IOWR_32DIRECT(csr_base, BURST_STATUS_REG, 1); // Clear Done
  }
  end_time = alt_nticks();

  hw_time_ms = (float)(end_time - start_time);
  printf("  -> HW Time: %.2f ms (Throughput: %.2f MB/s)\n", hw_time_ms,
         (float)(BYTES_TO_COPY * LOOP_COUNT) / (hw_time_ms * 1000.0));

  // 3. Verify Results
  printf("Step 3: Verifying HW DMA Integrity...\n");
  int errors = 0;
  for (i = 0; i < TEST_WORDS; i++) {
    unsigned int read_val = IORD_32DIRECT(dst_ptr, i * 4);
    if (read_val != (i + 0xEEEE0000)) {
      errors++;
    }
  }

  if (errors == 0) {
    printf("\n[SUCCESS] 100 iterations complete! DMA wins! 🎉\n");
  } else {
    printf("\n[FAILURE] Found %d errors. Integrity check failed. 😭\n", errors);
  }

  return 0;
}
