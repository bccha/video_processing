#include "st2110_common.h"

void flush_cpu_cache_hack() {
  printf("Forcing ARM Cortex-A9 L1/L2 Cache Eviction (16MB Memset)...\n");
  int size = 16 * 1024 * 1024; // 16MB completely annihilates the 512KB L2 cache
  volatile char *dummy = malloc(size);
  if (dummy) {
    memset((void *)dummy, 0xAA, size); // Force write allocation
    // Read back to ensure it was fully cached, pushing out our ST2110 packets!
    volatile char temp = 0;
    for (int i = 0; i < size; i += 4096) {
      temp += dummy[i];
    }
    free((void *)dummy);
  }
}

void test_st2110_pipeline_short(struct app_context *ctx, uint32_t *dst_ptr) {
  printf("\n--- Starting ST2110 Pipeline Test (Short Packets) ---\n");

  // --- PREPARE MOCK PACKET (Header + Payload) ---
  int SHORT_PACKET_COUNT = 200;
  uint32_t HEADER_BYTES = 62;
  uint32_t SHORT_PAYLOAD_BYTES = 1440;
  uint32_t SHORT_TOTAL_RX_BYTES = HEADER_BYTES + SHORT_PAYLOAD_BYTES; // 1502
  uint32_t SHORT_TOTAL_TX_BYTES = 1920;
  uint32_t aligned_offset_bytes = 1504;

  uint8_t *src_ptr = (uint8_t *)ctx->virtual_ddr_base;

  printf("Preparing %d mock packets at 0x%08X...\n", SHORT_PACKET_COUNT,
         FPGA_SHARED_DDR_PHYS_ADDR);

  for (int p = 0; p < SHORT_PACKET_COUNT; p++) {
    uint8_t *pkt_base = src_ptr + (p * aligned_offset_bytes);
    // printf("Packet %d at %p\n", p, pkt_base);

    // Zero out headers first
    for (int i = 0; i < HEADER_BYTES; i++) {
      pkt_base[i] = 0;
    }

    // Byte 43[7]: RTP Marker Bit
    pkt_base[43] = 0x80;

    // Byte 58-59: SRD Line = p
    pkt_base[58] = (p >> 8) & 0xFF;
    pkt_base[59] = p & 0xFF;
    pkt_base[60] = 0;
    pkt_base[61] = 0;

    // Set Payload (exactly matching Nios 'test_st2110_pipeline_short')
    for (int i = 0; i < SHORT_PAYLOAD_BYTES; i += 3) {
      uint8_t r = (p + i) & 0xFF;
      uint8_t g = (p + i + 1) & 0xFF;
      uint8_t b = (p + i + 2) & 0xFF;

      pkt_base[HEADER_BYTES + i] = r;
      pkt_base[HEADER_BYTES + i + 1] = g;
      pkt_base[HEADER_BYTES + i + 2] = b;
    }
  }

  printf("Streaming %d packets to DMA...\n", SHORT_PACKET_COUNT);

  // 1. Reset both DMA Engines
  msgdma_reset_pipeline(ctx->write_dma_csr, ctx->read_dma_csr);

  // 2. Stream descriptors and wait for completion per packet
  int timeout_error = msgdma_transmit_burst_pipeline(
      ctx->write_dma_csr, ctx->write_dma_desc, ctx->read_dma_csr,
      ctx->read_dma_desc, FPGA_SHARED_DDR_PHYS_ADDR,
      FPGA_FRAME_BUFFER_PHYS_ADDR, SHORT_PACKET_COUNT, aligned_offset_bytes,
      SHORT_TOTAL_TX_BYTES, SHORT_TOTAL_RX_BYTES);

  if (timeout_error) {
    uint32_t wr_status = IORD_32DIRECT(ctx->write_dma_csr, MSGDMA_CSR_STATUS);
    uint32_t rd_status = IORD_32DIRECT(ctx->read_dma_csr, MSGDMA_CSR_STATUS);
    printf("ERROR: DMA Write/Read Timeout!\n");
    printf("Write DMA CSR Status: 0x%08X\n", wr_status);
    printf("Read DMA CSR Status:  0x%08X\n", rd_status);
  }

  // --- Verifying Results (Same as Nios 't' Test) ---
  printf("\n--- Verifying Results ---\n");
  int errors = 0;
  for (int p = 0; p < SHORT_PACKET_COUNT; p++) {
    // printf("Packet %d Check (first 4 pixels):\n", p);
    uint32_t *pkt_dst = dst_ptr + (p * (SHORT_TOTAL_TX_BYTES / 4));
    // printf("Packet %d at %p\n", p, pkt_dst);
    for (int i = 0; i < 4; i++) {
      uint8_t r = (p + (i * 3)) & 0xFF;
      uint8_t g = (p + (i * 3) + 1) & 0xFF;
      uint8_t b = (p + (i * 3) + 2) & 0xFF;

      uint32_t expected = (r << 16) | (g << 8) | b;
      uint32_t actual = pkt_dst[i];

      if (actual != expected) {
        printf("Packet %d Mismatch at pixel %d: Expected 0x%08X, Got 0x%08X\n",
               p, i, expected, actual);
        errors++;
      }
    }
  }

  if (errors == 0) {
    printf("\n=> ST2110 Short Pipeline Test PASSED!\n\n");
  } else {
    printf("\n=> ST2110 Short Pipeline Test FAILED with %d errors.\n\n",
           errors);
  }
}

void test_st2110_pipeline_frame(struct app_context *ctx, uint32_t *dst_ptr) {
  printf("\n--- Starting ST2110 Full Frame (960x540) Pipeline Test ---\n");

  uint32_t FRAME_WIDTH = 960;
  uint32_t FRAME_HEIGHT = 540;
  uint32_t FRAME_PACKET_COUNT =
      360; // 960x540 = 518400 pixels. 1 packet = 1440 bytes = 480 pixels.
           // 518400/480 = 1080? Wait, 1080 packets for full frame.
  FRAME_PACKET_COUNT = 1080;

  uint32_t HEADER_BYTES = 62;
  uint32_t FRAME_PAYLOAD_BYTES = 1440;
  uint32_t FRAME_TOTAL_RX_BYTES = HEADER_BYTES + FRAME_PAYLOAD_BYTES;
  uint32_t FRAME_TOTAL_TX_BYTES = 1920;
  uint32_t aligned_offset_bytes = 1504;

  uint8_t *src_ptr = (uint8_t *)ctx->virtual_ddr_base;

  printf("Preparing %d mock packets (960x540 Frame) at 0x%08X...\n",
         FRAME_PACKET_COUNT, FPGA_SHARED_DDR_PHYS_ADDR);

  // Color bar colors (reversed order starting from Black)
  const uint32_t colors[8] = {0x000000, 0x0000FF, 0xFF0000, 0xFF00FF,
                              0x00FF00, 0x00FFFF, 0xFFFF00, 0xFFFFFF};
  int global_pixel_idx = 0;

  for (int p = 0; p < FRAME_PACKET_COUNT; p++) {
    uint8_t *pkt_base = src_ptr + (p * aligned_offset_bytes);

    // Zero out headers first
    for (int i = 0; i < HEADER_BYTES; i++) {
      pkt_base[i] = 0;
    }

    // Byte 43[7]: RTP Marker Bit = 1 if this is the last packet of the frame
    if (p == FRAME_PACKET_COUNT - 1) {
      pkt_base[43] = 0x80;
    }

    // Byte 58-59: SRD Line (y coordinate)
    // Byte 60-61: SRD Offset (byte offset in line)
    int current_y = global_pixel_idx / FRAME_WIDTH;
    int current_x = global_pixel_idx % FRAME_WIDTH;

    pkt_base[58] = (current_y >> 8) & 0xFF;
    pkt_base[59] = current_y & 0xFF;

    int byte_offset = current_x * 3;
    pkt_base[60] = (byte_offset >> 8) & 0xFF;
    pkt_base[61] = byte_offset & 0xFF;

    // Set Payload
    for (int i = 0; i < FRAME_PAYLOAD_BYTES; i += 3) {
      int pixel_x = global_pixel_idx % FRAME_WIDTH;
      // 8 color bars across 960 pixels means each bar is 120 pixels wide
      int color_idx = pixel_x / 120;
      if (color_idx > 7)
        color_idx = 7;

      uint32_t color = colors[color_idx];

      // Send standard ST2110-20 Network RGB byte order: Byte 0=R, 1=G, 2=B
      pkt_base[HEADER_BYTES + i] = (color >> 16) & 0xFF;    // R
      pkt_base[HEADER_BYTES + i + 1] = (color >> 8) & 0xFF; // G
      pkt_base[HEADER_BYTES + i + 2] = color & 0xFF;        // B

      global_pixel_idx++;
    }
  }

  // Clear Destination DDR (Off-screen Framebuffer)
  printf("Clearing Off-screen HDMI Framebuffer at 0x%08X...\n",
         FPGA_FRAME_BUFFER_PHYS_ADDR);
  for (int i = 0; i < (FRAME_PACKET_COUNT * FRAME_TOTAL_TX_BYTES) / 4; i++) {
    dst_ptr[i] = 0x00000000;
  }

  // 1. Reset both DMA Engines
  msgdma_reset_pipeline(ctx->write_dma_csr, ctx->read_dma_csr);

  // 2. Stream descriptors using BURST pipeline
  struct timeval start, end;
  gettimeofday(&start, NULL);

  int timeout_error = msgdma_transmit_burst_pipeline(
      ctx->write_dma_csr, ctx->write_dma_desc, ctx->read_dma_csr,
      ctx->read_dma_desc, FPGA_SHARED_DDR_PHYS_ADDR,
      FPGA_FRAME_BUFFER_PHYS_ADDR, FRAME_PACKET_COUNT, aligned_offset_bytes,
      FRAME_TOTAL_TX_BYTES, FRAME_TOTAL_RX_BYTES);

  gettimeofday(&end, NULL);
  long seconds = end.tv_sec - start.tv_sec;
  long microseconds = end.tv_usec - start.tv_usec;
  double elapsed_ms = (seconds * 1000.0) + (microseconds / 1000.0);

  if (timeout_error) {
    uint32_t wr_status = IORD_32DIRECT(ctx->write_dma_csr, MSGDMA_CSR_STATUS);
    uint32_t rd_status = IORD_32DIRECT(ctx->read_dma_csr, MSGDMA_CSR_STATUS);
    printf("ERROR: DMA Write/Read Timeout!\n");
    printf("Write DMA CSR Status: 0x%08X\n", wr_status);
    printf("Read DMA CSR Status:  0x%08X\n", rd_status);
  } else {
    printf("=> ST2110 Full Frame DMA Transfer Time: %.3f ms %.3f fps\n",
           elapsed_ms, 1000.0 / elapsed_ms);
  }

  // SWAP THE DISPLAY BUFFER POINTER!
  enable_hdmi_display(ctx);
}

int main() {
  struct app_context ctx;
  printf("\n--- ST2110 Pipeline Tools (Linux Port) ---\n");

  // Initialize Memory Mapping
  init_memory_mapping(&ctx);

  uint32_t frame_width = 960;
  uint32_t frame_height = 540;
  uint32_t fb_size = frame_width * frame_height * 4;
  void *virtual_frame_buffer =
      mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, ctx.fd_mem,
           FPGA_FRAME_BUFFER_PHYS_ADDR);
  if (virtual_frame_buffer == MAP_FAILED) {
    perror("mmap framebuffer");
    return 1;
  }

  // Initialize DMA Engines
  msgdma_init(ctx.read_dma_csr);
  msgdma_init(ctx.write_dma_csr);

  uint32_t *dst_ptr = (uint32_t *)virtual_frame_buffer;
  printf("Destination memory dynamically mapped at 0x%08X.\n",
         FPGA_FRAME_BUFFER_PHYS_ADDR);

  // Run the encapsulated full frame colorbar test
  test_st2110_pipeline_frame(&ctx, dst_ptr);

  cleanup_system(&ctx);
  munmap(virtual_frame_buffer, fb_size);
  return 0;
}
