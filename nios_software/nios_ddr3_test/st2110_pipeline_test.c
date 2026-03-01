#include "st2110_pipeline_test.h"
#include "alt_types.h"
#include "common.h"
#include "system.h"
#include "terasic_includes.h"
#include <io.h>
#include <stdio.h>

// mSGDMA Descriptor Format (Standard)
typedef struct {
  alt_u32 read_addr;
  alt_u32 write_addr;
  alt_u32 len;
  alt_u32 control;
} msgdma_standard_descriptor;

// mSGDMA Descriptor Control Bits - Realigned to Standard Altera Header
#define MSGDMA_CTRL_TX_CHANNEL(x) (((x) & 0xFF) << 16)
#define MSGDMA_CTRL_GENERATE_SOP (1 << 8)
#define MSGDMA_CTRL_GENERATE_EOP (1 << 9)
#define MSGDMA_CTRL_PARK_READS (1 << 10)
#define MSGDMA_CTRL_PARK_WRITES (1 << 11)
#define MSGDMA_CTRL_END_ON_EOP (1 << 12)
#define MSGDMA_CTRL_END_ON_LEN (1 << 13)
#define MSGDMA_CTRL_TRANSFER_COMPLETE_IRQ_EN (1 << 14)
#define MSGDMA_CTRL_EARLY_TERMINATION_IRQ_EN (1 << 15)
#define MSGDMA_CTRL_GO (1 << 31)

// mSGDMA CSR Register Offsets
#define MSGDMA_CSR_STATUS (0x00)
#define MSGDMA_CSR_CONTROL (0x04)
#define MSGDMA_CSR_FILL_LEVEL (0x08)

// mSGDMA CSR Status Bits
#define MSGDMA_STATUS_BUSY (1 << 0)

// -----------------------------------------------------------------------------
// Test Parameters for Short Test (4 packets = 6008 bytes exactly for LCM of 4
// and 1502)
// -----------------------------------------------------------------------------
#define SHORT_PACKET_COUNT 10
#define SHORT_PAYLOAD_BYTES 1440
#define SHORT_TOTAL_RX_BYTES (62 + SHORT_PAYLOAD_BYTES) // 1502 bytes
#define SHORT_TOTAL_TX_BYTES                                                   \
  (SHORT_PAYLOAD_BYTES / 3 * 4) // 480 pixels * 4 = 1920 bytes

// -----------------------------------------------------------------------------
// Test Parameters for 960x540 Frame
// -----------------------------------------------------------------------------
#define FRAME_WIDTH 960
#define FRAME_HEIGHT 540
#define PIXELS_PER_PACKET 480
#define FRAME_PACKET_COUNT                                                     \
  ((FRAME_WIDTH * FRAME_HEIGHT) / PIXELS_PER_PACKET) // 1080 packets

#define HEADER_BYTES 62
#define FRAME_PAYLOAD_BYTES (PIXELS_PER_PACKET * 3)               // 1440
#define FRAME_TOTAL_RX_BYTES (HEADER_BYTES + FRAME_PAYLOAD_BYTES) // 1502
#define FRAME_TOTAL_TX_BYTES (PIXELS_PER_PACKET * 4)              // 1920 bytes

// -----------------------------------------------------------------------------
// DDR Memory Regions for Test
// -----------------------------------------------------------------------------
#define TEST_SRC_ADDR                                                          \
  (ADDRESS_SPAN_EXTENDER_0_WINDOWED_SLAVE_BASE +                               \
   0x01000000) // 16MB offset (Ring buffer)
#define TEST_DST_ADDR                                                          \
  (ADDRESS_SPAN_EXTENDER_0_WINDOWED_SLAVE_BASE) // HDMI Framebuffer (0x20000000
                                                // physical)
#define TEST_SHORT_DST_ADDR                                                    \
  (ADDRESS_SPAN_EXTENDER_0_WINDOWED_SLAVE_BASE +                               \
   0x02000000) // 32MB offset for short test

// Absolute physical addresses in HPS DDR for DMA masters connected directly to
// f2h_sdram
#define PHYS_SRC_ADDR 0x21000000
#define PHYS_DST_ADDR 0x20000000 // Framebuffer 0 (Initial)
#define PHYS_SHORT_DST_ADDR 0x22000000
#define PHYS_FRAME_DST_ADDR 0x24000000 // Framebuffer 1 (Ping-Pong target)

#define TEST_FRAME_DST_ADDR                                                    \
  (ADDRESS_SPAN_EXTENDER_0_WINDOWED_SLAVE_BASE +                               \
   0x04000000) // 64MB offset for off-screen ping-pong buffer

// Base Address for HDMI Sync Generator CSR (now using HDMI_SYNC_BASE from
// system.h)

// Helper function to send a single packet via mSGDMA
void send_st2110_packet(alt_u32 src_addr, alt_u32 dst_addr, alt_u32 rx_len,
                        alt_u32 tx_len) {
  // Push Write Descriptor (Destination)
  msgdma_standard_descriptor wr_desc;
  wr_desc.read_addr = 0;
  wr_desc.write_addr = dst_addr;
  wr_desc.len = tx_len;
  wr_desc.control = MSGDMA_CTRL_GO | MSGDMA_CTRL_END_ON_EOP;

  IOWR_32DIRECT(RX_DMA_WRITE_DESCRIPTOR_SLAVE_BASE, 0x00, wr_desc.read_addr);
  IOWR_32DIRECT(RX_DMA_WRITE_DESCRIPTOR_SLAVE_BASE, 0x04, wr_desc.write_addr);
  IOWR_32DIRECT(RX_DMA_WRITE_DESCRIPTOR_SLAVE_BASE, 0x08, wr_desc.len);
  IOWR_32DIRECT(RX_DMA_WRITE_DESCRIPTOR_SLAVE_BASE, 0x0C, wr_desc.control);

  // Push Read Descriptor (Source)
  msgdma_standard_descriptor rd_desc;
  rd_desc.read_addr = src_addr;
  rd_desc.write_addr = 0;
  rd_desc.len = rx_len;
  rd_desc.control =
      MSGDMA_CTRL_GO | MSGDMA_CTRL_GENERATE_SOP | MSGDMA_CTRL_GENERATE_EOP;

  IOWR_32DIRECT(RX_DMA_READ_DESCRIPTOR_SLAVE_BASE, 0x00, rd_desc.read_addr);
  IOWR_32DIRECT(RX_DMA_READ_DESCRIPTOR_SLAVE_BASE, 0x04, rd_desc.write_addr);
  IOWR_32DIRECT(RX_DMA_READ_DESCRIPTOR_SLAVE_BASE, 0x08, rd_desc.len);
  IOWR_32DIRECT(RX_DMA_READ_DESCRIPTOR_SLAVE_BASE, 0x0C, rd_desc.control);

  // Note: Optimized helper. Does NOT block.
  // It pushes to the FIFO and returns immediately so Nios can keep pushing!
}

void test_st2110_pipeline_short() {
  printf("\n--- Starting ST2110 Pipeline Test (Short: 3 Packets) ---\n");

  alt_u8 *src_ptr = (alt_u8 *)(TEST_SRC_ADDR | CACHE_BYPASS_MASK);
  alt_u32 *dst_ptr = (alt_u32 *)(TEST_SHORT_DST_ADDR | CACHE_BYPASS_MASK);

  // 1. Prepare Mock ST2110 Packets (Source DDR)
  printf("Preparing %d mock packets at 0x%08X...\n", SHORT_PACKET_COUNT,
         TEST_SRC_ADDR);
  for (int p = 0; p < SHORT_PACKET_COUNT; p++) {
    // ALIGNMENT FIX: Even though the packet is 1502 bytes, we must place each
    // packet on a 4-byte (32-bit) aligned boundary in DDR memory so the MSGDMA
    // standard descriptor doesn't truncate the unaligned address and shift the
    // data!
    alt_u32 aligned_offset_bytes =
        1504; // 1504 is the next multiple of 4 after 1502
    alt_u8 *pkt_base = src_ptr + (p * aligned_offset_bytes);

    // Zero out headers first
    for (int i = 0; i < HEADER_BYTES; i++) {
      pkt_base[i] = 0;
    }

    // Set specific header fields
    // Byte 43[7]: RTP Marker Bit = 1 (End of Frame)
    pkt_base[43] = 0x80;

    // Byte 58-59: SRD Line = p (0, 1, 2...)
    pkt_base[58] = (p >> 8) & 0xFF;
    pkt_base[59] = p & 0xFF;

    // Byte 60-61: SRD Offset = 0
    pkt_base[60] = 0;
    pkt_base[61] = 0;

    // Set Payload (1440 bytes = 480 RGB pixels)
    for (int i = 0; i < SHORT_PAYLOAD_BYTES; i += 3) {
      alt_u8 r = (p + i) & 0xFF;
      alt_u8 g = (p + i + 1) & 0xFF;
      alt_u8 b = (p + i + 2) & 0xFF;
      pkt_base[HEADER_BYTES + i] = r;
      pkt_base[HEADER_BYTES + i + 1] = g;
      pkt_base[HEADER_BYTES + i + 2] = b;
    }
  }

  // Clear Destination DDR
  printf("Clearing destination memory at 0x%08X...\n", TEST_SHORT_DST_ADDR);
  for (int i = 0; i < (SHORT_PACKET_COUNT * SHORT_TOTAL_TX_BYTES) / 4; i++) {
    dst_ptr[i] = 0x00000000;
  }

  // Flush cache
  alt_dcache_flush_all();

  // 2. Configure mSGDMA Write (Destination) and Read (Source)
  // 1. Issue Reset Command
  IOWR_32DIRECT(RX_DMA_WRITE_CSR_BASE, MSGDMA_CSR_CONTROL, (1 << 1));

  // 2. Wait for Hardware to Finish Resetting (Poll Status Bit 6)
  while (IORD_32DIRECT(RX_DMA_WRITE_CSR_BASE, MSGDMA_CSR_STATUS) & (1 << 6)) {
    // Loop actively while resetting
  }

  // 3. Clear existing status bits
  IOWR_32DIRECT(RX_DMA_WRITE_CSR_BASE, MSGDMA_CSR_STATUS, 0xFFFFFFFF);

  // 1. Issue Reset Command
  IOWR_32DIRECT(RX_DMA_READ_CSR_BASE, MSGDMA_CSR_CONTROL, (1 << 1));

  // 2. Wait for Hardware to Finish Resetting (Poll Status Bit 6)
  while (IORD_32DIRECT(RX_DMA_READ_CSR_BASE, MSGDMA_CSR_STATUS) & (1 << 6)) {
    // Loop actively while resetting
  }

  // 3. Clear existing status bits
  IOWR_32DIRECT(RX_DMA_READ_CSR_BASE, MSGDMA_CSR_STATUS, 0xFFFFFFFF);

  // 3. Send packets via helper
  printf("Streaming %d packets to DMA...\n", SHORT_PACKET_COUNT);
  printf("Waiting for DMA to complete...\n");

  unsigned long long dma_start_cycles = get_total_cycles();

  for (int p = 0; p < SHORT_PACKET_COUNT; p++) {
    alt_u32 aligned_offset_bytes = 1504;
    alt_u32 src = PHYS_SRC_ADDR + (p * aligned_offset_bytes);
    alt_u32 dst = PHYS_SHORT_DST_ADDR + (p * SHORT_TOTAL_TX_BYTES);

    send_st2110_packet(src, dst, SHORT_TOTAL_RX_BYTES, SHORT_TOTAL_TX_BYTES);
  }

  // 4. Wait for DMA completion
  int timeout = 10000000;

  // Wait for Read DMA to finish naturally first
  while ((IORD_32DIRECT(RX_DMA_READ_CSR_BASE, MSGDMA_CSR_STATUS) &
          MSGDMA_STATUS_BUSY) &&
         timeout > 0) {
    timeout--;
  }

  while ((IORD_32DIRECT(RX_DMA_WRITE_CSR_BASE, MSGDMA_CSR_STATUS) &
          MSGDMA_STATUS_BUSY) &&
         timeout > 0) {
    timeout--;
  }

  unsigned long long dma_end_cycles = get_total_cycles();
  unsigned long long dma_elapsed_cycles = dma_end_cycles - dma_start_cycles;
  // dma_elapsed_ms = (cycles / 50000)
  alt_u32 elapsed_ms_int = (alt_u32)(dma_elapsed_cycles / 50000);
  alt_u32 elapsed_ms_frac =
      (alt_u32)((dma_elapsed_cycles % 50000) * 1000 / 50000);

  alt_u32 wr_status = IORD_32DIRECT(RX_DMA_WRITE_CSR_BASE, MSGDMA_CSR_STATUS);
  alt_u32 rd_status = IORD_32DIRECT(RX_DMA_READ_CSR_BASE, MSGDMA_CSR_STATUS);

  if (timeout == 0) {
    printf("ERROR: DMA Write Timeout!\n");
    printf("Write DMA CSR Status: 0x%08lX\n", wr_status);
    printf("Read DMA CSR Status:  0x%08lX\n", rd_status);
  } else {
    printf("DMA Completed Successfully. WrStatus=0x%08lX, RdStatus=0x%08lX\n",
           wr_status, rd_status);
  }

  // 5. Verify Results
  printf("\n--- Verifying Results ---\n");
  int errors = 0;
  for (int p = 0; p < SHORT_PACKET_COUNT; p++) {
    printf("Packet %d Check (first 4 pixels):\n", p);
    alt_u32 *pkt_dst = dst_ptr + (p * (SHORT_TOTAL_TX_BYTES / 4));

    for (int i = 0; i < 4; i++) {
      alt_u8 r = (p + (i * 3)) & 0xFF;
      alt_u8 g = (p + (i * 3) + 1) & 0xFF;
      alt_u8 b = (p + (i * 3) + 2) & 0xFF;

      // Hardware Output is `{8'd0, R, G, B}` which translates to Memory as:
      // Byte0=B, Byte1=G, Byte2=R, Byte3=00 (Little-Endian)
      // When Nios II reads this as Int32:
      // [31:24]=00, [23:16]=R, [15:8]=G, [7:0]=B  -> 0x00RRGGBB
      alt_u32 expected = (r << 16) | (g << 8) | b;
      alt_u32 actual = pkt_dst[i];

      if (actual != expected) {
        printf("  Mismatch at pixel %d: Expected 0x%08lX, Got 0x%08lX\n", i,
               expected, actual);
        errors++;
      } else {
        printf("  Match pixel %d: 0x%08lX\n", i, actual);
      }
    }
  }

  if (errors == 0) {
    printf("\n=> ST2110 Short Pipeline Test PASSED!\n");
    printf("=> DMA Transfer Time: %lu.%03lu ms\n\n", elapsed_ms_int,
           elapsed_ms_frac);
  } else {
    printf("\n=> ST2110 Short Pipeline Test FAILED with %d errors.\n\n",
           errors);
  }
}

void test_st2110_pipeline_frame() {
  printf("\n--- Starting ST2110 Full Frame (960x540) Pipeline Test ---\n");

  alt_u8 *src_ptr = (alt_u8 *)(TEST_SRC_ADDR | CACHE_BYPASS_MASK);
  alt_u32 *dst_ptr = (alt_u32 *)(TEST_FRAME_DST_ADDR |
                                 CACHE_BYPASS_MASK); // Use ping-pong buffer

  // 1. Prepare Mock ST2110 Packets for a full 960x540 frame
  printf("Preparing %d mock packets (960x540 Frame) at 0x%08X...\n",
         FRAME_PACKET_COUNT, TEST_SRC_ADDR);

  // Color bar colors (reversed order from standard to distinguish from menu
  // option 4)
  const alt_u32 colors[8] = {0x000000, 0x0000FF, 0xFF0000, 0xFF00FF,
                             0x00FF00, 0x00FFFF, 0xFFFF00, 0xFFFFFF};
  int global_pixel_idx = 0;

  alt_u32 aligned_offset_bytes = 1504; // Pad to aligned 32-bit boundary

  for (int p = 0; p < FRAME_PACKET_COUNT; p++) {
    alt_u8 *pkt_base = src_ptr + (p * aligned_offset_bytes);

    // Zero out headers first
    for (int i = 0; i < HEADER_BYTES; i++) {
      pkt_base[i] = 0;
    }

    // Set specific header fields
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

    // Offset is in terms of ST2110 active video bytes (RGB = 3 bytes/pixel)
    int byte_offset = current_x * 3;
    pkt_base[60] = (byte_offset >> 8) & 0xFF;
    pkt_base[61] = byte_offset & 0xFF;

    // Set Payload
    for (int i = 0; i < FRAME_PAYLOAD_BYTES; i += 3) {
      // Fix: `global_pixel_idx` represents actual RGB pixel tuples (0 to
      // 960*540-1)
      int pixel_x = global_pixel_idx % FRAME_WIDTH;
      // 8 color bars across 960 pixels means each bar is 120 *pixels* wide
      int color_idx = pixel_x / 120;
      if (color_idx > 7)
        color_idx = 7;

      alt_u32 color = colors[color_idx];

      // Send standard ST2110-20 Network RGB byte order: Byte 0=R, 1=G, 2=B
      pkt_base[HEADER_BYTES + i] = (color >> 16) & 0xFF;    // R
      pkt_base[HEADER_BYTES + i + 1] = (color >> 8) & 0xFF; // G
      pkt_base[HEADER_BYTES + i + 2] = color & 0xFF;        // B

      global_pixel_idx++;
    }
  }

  // Flush cache to ensure DMA sees the data
  alt_dcache_flush_all();

  // Clear Destination DDR (Off-screen Framebuffer)
  printf("Clearing Off-screen HDMI Framebuffer at 0x%08X...\n",
         TEST_FRAME_DST_ADDR);
  for (int i = 0; i < (FRAME_PACKET_COUNT * FRAME_TOTAL_TX_BYTES) / 4; i++) {
    dst_ptr[i] = 0x00000000;
  }

  // Run the DMA Only Transfer Module (refactored to avoid code duplication)
  if (test_st2110_pipeline_frame_dma_only() != 0) {
    printf("\n=> ST2110 Pipeline Test FAILED (DMA Timeout).\n\n");
    return;
  }

  // SWAP THE DISPLAY BUFFER POINTER!
  printf("Swapping HDMI Sync Gen Frame Pointer to 0x%08X...\n",
         PHYS_FRAME_DST_ADDR);
  IOWR_32DIRECT(HDMI_SYNC_BASE | CACHE_BYPASS_MASK, 6 * 4, PHYS_FRAME_DST_ADDR);

  // 5. Verify Results (Sample check: check 1st packet and last packet)
  printf("\n--- Verifying Sample Results ---\n");
  int errors = 0;

  // Check first 4 pixels of the first packet
  printf("Checking Top-Left Corner (Expected White: 0x00FFFFFF):\n");
  for (int i = 0; i < 4; i++) {
    alt_u32 actual = dst_ptr[i];
    // Endianness byte swap for Little-Endian Nios CPU
    // Expected is White (FF FF FF) so it's the same regardless, but let's be
    // technically correct
    if (actual != 0x00FFFFFF) {
      printf("  Mismatch at pixel %d: Expected 0x00FFFFFF, Got 0x%08lX\n", i,
             actual);
      errors++;
    }
  }

  // Check first 4 pixels of the last packet
  int last_pkt_start = (FRAME_PACKET_COUNT - 1) * (FRAME_TOTAL_TX_BYTES / 4);
  printf("Checking Bottom-Left Corner of last packet (Expected Black: "
         "0x00000000 or White depending on alignment):\n");
  // Note: The last packet starts at pixel index 517920.
  // 517920 % 960 = 480.  480 / 120 = 4 (Magenta). Let's just print it.
  for (int i = 0; i < 4; i++) {
    alt_u32 actual = dst_ptr[last_pkt_start + i];
    printf("  Last Pkt Pixel %d: 0x%08lX\n", i, actual);
  }

  // --- MEMORY DUMP FOR RGB DEBUGGING ---
  printf("\n--- Memory Dump (First 16 Pixels) ---\n");
  for (int i = 0; i < 16; i++) {
    printf("Pixel %3d: 0x%08lX\n", i, dst_ptr[i]);
  }
  printf("-------------------------------------\n");

  if (errors == 0) {
    printf("\n=> ST2110 Pipeline Test (960x540 Frame) PASSED!\n");
    printf("=> You can now use option [8] 'DMA Video Stream Test' to view this "
           "frame on the HDMI output.\n\n");
  } else {
    printf("\n=> ST2110 Pipeline Test FAILED with %d errors.\n\n", errors);
  }
}

int test_st2110_pipeline_frame_dma_only() {
  printf("\n--- Running ST2110 Full Frame DMA Transfer ONLY ---\n");

  alt_u32 aligned_offset_bytes = 1504; // Pad to aligned 32-bit boundary

  // 1. Issue Reset Command
  IOWR_32DIRECT(RX_DMA_WRITE_CSR_BASE, MSGDMA_CSR_CONTROL, (1 << 1));
  while (IORD_32DIRECT(RX_DMA_WRITE_CSR_BASE, MSGDMA_CSR_STATUS) & (1 << 6)) {
  }
  IOWR_32DIRECT(RX_DMA_WRITE_CSR_BASE, MSGDMA_CSR_STATUS, 0xFFFFFFFF);

  IOWR_32DIRECT(RX_DMA_READ_CSR_BASE, MSGDMA_CSR_CONTROL, (1 << 1));
  while (IORD_32DIRECT(RX_DMA_READ_CSR_BASE, MSGDMA_CSR_STATUS) & (1 << 6)) {
  }
  IOWR_32DIRECT(RX_DMA_READ_CSR_BASE, MSGDMA_CSR_STATUS, 0xFFFFFFFF);

  printf("Streaming %d packets to DMA...\n", FRAME_PACKET_COUNT);

  unsigned long long dma_start_cycles = get_total_cycles();

  for (int p = 0; p < FRAME_PACKET_COUNT; p++) {
    alt_u32 src = PHYS_SRC_ADDR + (p * aligned_offset_bytes);
    alt_u32 dst = PHYS_FRAME_DST_ADDR + (p * FRAME_TOTAL_TX_BYTES);

    send_st2110_packet(src, dst, FRAME_TOTAL_RX_BYTES, FRAME_TOTAL_TX_BYTES);
  }

  int timeout = 10000000;
  while ((IORD_32DIRECT(RX_DMA_WRITE_CSR_BASE, MSGDMA_CSR_STATUS) &
          MSGDMA_STATUS_BUSY) &&
         timeout > 0) {
    timeout--;
  }

  unsigned long long dma_end_cycles = get_total_cycles();
  unsigned long long dma_elapsed_cycles = dma_end_cycles - dma_start_cycles;

  // Calculate Time (ms)
  alt_u32 elapsed_ms_int = (alt_u32)(dma_elapsed_cycles / 50000);
  alt_u32 elapsed_ms_frac =
      (alt_u32)((dma_elapsed_cycles % 50000) * 1000 / 50000);

  // Calculate FPS (1000.0 / ms = 50,000,000 / dt_cycles)
  alt_u32 fps_int = 0;
  alt_u32 fps_frac = 0;
  if (dma_elapsed_cycles > 0) {
    fps_int = (alt_u32)(50000000ULL / dma_elapsed_cycles);
    fps_frac = (alt_u32)((50000000ULL % dma_elapsed_cycles) * 100 /
                         dma_elapsed_cycles);
  }

  if (timeout == 0) {
    printf("ERROR: DMA Write Timeout!\n");
    return -1;
  } else {
    printf("=> DMA Transfer Time: %lu.%03lu ms (approx %lu.%02lu fps "
           "equivalent)\n\n",
           elapsed_ms_int, elapsed_ms_frac, fps_int, fps_frac);
    return 0;
  }
}
