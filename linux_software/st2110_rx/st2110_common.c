#include "st2110_common.h"

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
  usleep(100);
  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_WRITE_ADDR, 0x00000000);
  usleep(100);
  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_LENGTH, length);
  usleep(100);
  IOWR_32DIRECT(descriptor_base, MSGDMA_DESC_CONTROL,
                MSGDMA_CTRL_GO | MSGDMA_CTRL_GENERATE_SOP |
                    MSGDMA_CTRL_GENERATE_EOP);
  usleep(100);
}

void msgdma_read_stream_push_chunked(volatile uint32_t *descriptor_base,
                                     uint32_t src_addr, uint32_t length,
                                     int is_sop, int is_eop) {
  uint32_t control = MSGDMA_CTRL_GO; // GO (31)
  if (is_eop)
    control |= MSGDMA_CTRL_GENERATE_EOP; // GEN_EOP (bit 9)
  if (is_sop)
    control |= MSGDMA_CTRL_GENERATE_SOP; // GEN_SOP (bit 8)

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
                MSGDMA_CTRL_GO | MSGDMA_CTRL_END_ON_EOP);
}

void msgdma_reset_pipeline(volatile uint32_t *write_csr,
                           volatile uint32_t *read_csr) {
  // 1. Issue Reset Command (Software Reset = bit 1 in CONTROL register)
  IOWR_32DIRECT(write_csr, MSGDMA_CSR_CONTROL, (1 << 1)); // bit 1 is Reset
  while (IORD_32DIRECT(write_csr, MSGDMA_CSR_STATUS) &
         (1 << 6)) { // bit 6 is Resetting
  }
  // Clear any pending status bits
  IOWR_32DIRECT(write_csr, MSGDMA_CSR_STATUS, 0xFFFFFFFF);

  IOWR_32DIRECT(read_csr, MSGDMA_CSR_CONTROL, (1 << 1));
  while (IORD_32DIRECT(read_csr, MSGDMA_CSR_STATUS) & (1 << 6)) {
  }
  // Clear any pending status bits
  IOWR_32DIRECT(read_csr, MSGDMA_CSR_STATUS, 0xFFFFFFFF);
}

int msgdma_transmit_burst_pipeline(
    volatile uint32_t *write_csr, volatile uint32_t *write_desc,
    volatile uint32_t *read_csr, volatile uint32_t *read_desc,
    uint32_t src_base_addr, uint32_t dst_base_addr, int packet_count,
    uint32_t src_offset, uint32_t write_bytes, uint32_t read_bytes) {

  int packets_pushed = 0;

  while (packets_pushed < packet_count) {
    // 1. Check if we have space in both FIFOs
    int write_full = IORD_32DIRECT(write_csr, MSGDMA_CSR_STATUS) &
                     ALTERA_MSGDMA_CSR_DESCRIPTOR_BUFFER_FULL_MASK;
    int read_full = IORD_32DIRECT(read_csr, MSGDMA_CSR_STATUS) &
                    ALTERA_MSGDMA_CSR_DESCRIPTOR_BUFFER_FULL_MASK;

    if (!write_full && !read_full) {
      // Both have space: push the next descriptor pair
      uint32_t src = src_base_addr + (packets_pushed * src_offset);
      uint32_t dst = dst_base_addr + (packets_pushed * write_bytes);

      msgdma_write_stream_push(write_desc, dst, write_bytes);
      msgdma_read_stream_push_chunked(read_desc, src, read_bytes, 1, 1);

      packets_pushed++;
    } else {
      // If either FIFO is full, just wait a tiny bit for the hardware to drain
      // them In a real burst we just loop (or usleep) until space frees up
      // again
      usleep(1);
    }
  }

  // Once ALL descriptors are successfully pushed into the FIFOs,
  // wait for the DMA engines to completely finish all queued tasks.
  int timeout = 100000000;
  while ((IORD_32DIRECT(read_csr, MSGDMA_CSR_STATUS) &
          ALTERA_MSGDMA_CSR_BUSY_MASK) &&
         timeout > 0) {
    timeout--;
  }
  while ((IORD_32DIRECT(write_csr, MSGDMA_CSR_STATUS) &
          ALTERA_MSGDMA_CSR_BUSY_MASK) &&
         timeout > 0) {
    timeout--;
  }

  if (timeout <= 0) {
    return -1; // Timeout Error
  }

  return 0; // Success
}
