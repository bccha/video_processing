#include "st2110_common.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

// --- Memory Initialization ---
void init_memory_mapping(struct app_context *ctx) {
  if ((ctx->fd_mem = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) {
    perror("Error opening /dev/mem (Must run as root/sudo)");
    exit(EXIT_FAILURE);
  }

  // Map DDR Buffer
  ctx->virtual_ddr_base =
      mmap(NULL, RING_BUFFER_SIZE_BYTES, (PROT_READ | PROT_WRITE), MAP_SHARED,
           ctx->fd_mem, (off_t)FPGA_SHARED_DDR_PHYS_ADDR);
  if (ctx->virtual_ddr_base == MAP_FAILED) {
    perror("Error mapping physical DDR memory");
    close(ctx->fd_mem);
    exit(EXIT_FAILURE);
  }

  // Map DMA Registers on LWH2F
  ctx->virtual_lwh2f_base = mmap(NULL, LWH2F_SPAN, (PROT_READ | PROT_WRITE),
                                 MAP_SHARED, ctx->fd_mem, (off_t)LWH2F_BASE);
  if (ctx->virtual_lwh2f_base == MAP_FAILED) {
    perror("Error mapping LWH2F bridge memory");
    munmap(ctx->virtual_ddr_base, RING_BUFFER_SIZE_BYTES);
    close(ctx->fd_mem);
    exit(EXIT_FAILURE);
  }

  ctx->read_dma_csr =
      (uint32_t *)(ctx->virtual_lwh2f_base + RX_DMA_READ_CSR_OFFSET);
  ctx->read_dma_desc = (uint32_t *)(ctx->virtual_lwh2f_base +
                                    RX_DMA_READ_DESCRIPTOR_SLAVE_OFFSET);

  ctx->write_dma_csr =
      (uint32_t *)(ctx->virtual_lwh2f_base + RX_DMA_WRITE_CSR_OFFSET);
  ctx->write_dma_desc = (uint32_t *)(ctx->virtual_lwh2f_base +
                                     RX_DMA_WRITE_DESCRIPTOR_SLAVE_OFFSET);

  ctx->hdmi_csr = (uint32_t *)(ctx->virtual_lwh2f_base + HDMI_SYNC_GEN_OFFSET);

  printf("Successfully mapped DDR (0x%08X) -> %p\n", FPGA_SHARED_DDR_PHYS_ADDR,
         ctx->virtual_ddr_base);
  printf("Successfully mapped LWH2F -> %p\n", ctx->virtual_lwh2f_base);
}

// --- HDMI Display Enable ---
void enable_hdmi_display(struct app_context *ctx) {
  printf("\nConfiguring HDMI Sync Generator to display processed frame...\n");
  // 1. Point Video DMA to our processed output buffer
  IOWR_32DIRECT(ctx->hdmi_csr, REG_FRAME_PTR, FPGA_FRAME_BUFFER_PHYS_ADDR);
  // 2. Set Mode 8 (DMA Stream)
  IOWR_32DIRECT(ctx->hdmi_csr, REG_PATTERN_MODE, 8);
  // 3. Enable Continuous Video DMA
  IOWR_32DIRECT(ctx->hdmi_csr, REG_GLOBAL_CTRL, 0x02);
  printf(">>> HDMI Display Enabled! <<<\n\n");
}

// --- Cleanup ---
void cleanup_system(struct app_context *ctx) {
  munmap(ctx->virtual_lwh2f_base, LWH2F_SPAN);
  close(ctx->fd_mem);

  if (ctx->raw_sock >= 0) {
    close(ctx->raw_sock);
  }
}

// ============================================================================
// MSGDMA Helpers (Adapted from NPU Test)
// ============================================================================
void msgdma_init(volatile uint32_t *csr_base) {
  IOWR_32DIRECT(csr_base, MSGDMA_CSR_STATUS, 0xFFFFFFFF);
  IOWR_32DIRECT(csr_base, MSGDMA_CSR_CONTROL, 0x00000000);
}

void msgdma_read_stream_push(volatile uint32_t *descriptor_base,
                             uint32_t src_addr, uint32_t length) {
  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_READ_ADDR, src_addr);
  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_WRITE_ADDR, 0x00000000);
  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_LENGTH, length);
  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_CONTROL,
                0x8000C000); // GO (31) | GEN_EOP(15) | GEN_SOP(14)
}

void msgdma_read_stream_push_chunked(volatile uint32_t *descriptor_base,
                                     uint32_t src_addr, uint32_t length,
                                     int is_sop, int is_eop) {
  uint32_t control = 0x80000000; // GO (31)
  if (is_eop)
    control |= 0x00008000; // GEN_EOP (15)
  if (is_sop)
    control |= 0x00004000; // GEN_SOP (14)

  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_READ_ADDR, src_addr);
  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_WRITE_ADDR, 0x00000000);
  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_LENGTH, length);
  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_CONTROL, control);
}

void msgdma_write_stream_push(volatile uint32_t *descriptor_base,
                              uint32_t dst_addr, uint32_t length) {
  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_READ_ADDR, 0x00000000);
  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_WRITE_ADDR, dst_addr);
  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_LENGTH, length);
  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_CONTROL,
                0x80001000); // GO (31) | End on EOP(12)
}
