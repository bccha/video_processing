#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * ==============================================================================
 * ST 2110 Static Payload Transmitter (ARM Linux)
 * ==============================================================================
 * Description:
 * This program reads a pre-generated ST2110 binary file (containing 1 frame of
 * ST2110 packets with headers + RGB payload), copies it into the FPGA's shared
 * DDR Ring Buffer, and configures/starts the MSGDMA (rx_dma_read) to stream
 * it into the hardware processing pipeline.
 * ==============================================================================
 */

#include "st2110_common.h"

// --- File Loading ---
long load_binary_to_ddr(struct app_context *ctx, const char *filepath) {
  FILE *fp = fopen(filepath, "rb");
  if (!fp) {
    perror("Error opening ST2110 binary file");
    exit(EXIT_FAILURE);
  }

  // Get file size
  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (file_size > RING_BUFFER_SIZE_BYTES) {
    printf("Error: File size (%ld) exceeds DDR Ring Buffer size (%d)\n",
           file_size, RING_BUFFER_SIZE_BYTES);
    fclose(fp);
    exit(EXIT_FAILURE);
  }

  // Read the file into a temporary buffer in ARM RAM first.
  // We do NOT want to `fread` directly into the mapped FPGA DDR space because
  // the Linux kernel's aggressive file caching and burst AXI transfers will
  // completely saturate the bridge and starve the HDMI Video DMA, causing
  // tearing.
  uint8_t *temp_buffer = (uint8_t *)malloc(file_size);
  if (!temp_buffer) {
    perror("Error allocating temporary buffer in ARM RAM");
    fclose(fp);
    exit(EXIT_FAILURE);
  }

  size_t read_bytes = fread(temp_buffer, 1, file_size, fp);
  fclose(fp);

  if (read_bytes != file_size) {
    printf("Error: Expected %ld bytes but only read %zu\n", file_size,
           read_bytes);
    free(temp_buffer);
    exit(EXIT_FAILURE);
  }

  // --- Throttled Memory Copy to FPGA DDR3 ---
  // Copy the data from ARM RAM to FPGA DDR3 in small chunks, with a slight
  // delay between chunks. This yields the AXI bridge and DDR3 bandwidth
  // back to the HDMI Sync Generator so the screen doesn't tear while loading.
  printf(
      "[INFO] Copying file to FPGA DDR3 (Throttled to prevent tearing)...\n");
  long bytes_remaining = file_size;
  uint32_t offset = 0;
  uint32_t CHUNK_SIZE = 65536; // 64KB per chunk

  while (bytes_remaining > 0) {
    uint32_t copy_size =
        (bytes_remaining > CHUNK_SIZE) ? CHUNK_SIZE : bytes_remaining;

    // Copy one chunk to the memory-mapped FPGA DDR3 space
    memcpy((uint8_t *)ctx->virtual_ddr_base + offset, temp_buffer + offset,
           copy_size);

    // Yield the AXI bridge (1ms delay)
    usleep(1000);

    offset += copy_size;
    bytes_remaining -= copy_size;
  }

  free(temp_buffer);

  if (read_bytes != file_size) {
    printf("Error: Expected %ld bytes but only read %zu\n", file_size,
           read_bytes);
    exit(EXIT_FAILURE);
  }

  printf("Successfully loaded %ld bytes from %s into DDR memory.\n", file_size,
         filepath);
  return file_size;
}

// --- Start MSGDMA Transfer (Both Read & Write) ---
void start_dma_transfer(struct app_context *ctx, long read_length_bytes) {
  // ----------------------------------------------------
  // 1. Initialize DMA Engines
  // ----------------------------------------------------
  msgdma_init(ctx->write_dma_csr);
  msgdma_init(ctx->read_dma_csr);

  // ----------------------------------------------------
  // 2. Configure rx_dma_write (To receive the RTL output)
  // ----------------------------------------------------
  // The processed frame = 960x540 pixels. The aligner outputs 32-bits per
  // pixel! So 960 * 540 * 4 bytes = 2,073,600 bytes.
  // To avoid screen tearing ("지지직"), we write to a hidden back-buffer offset
  // (e.g., +5MB)
  uint32_t hidden_buffer_addr = FPGA_FRAME_BUFFER_PHYS_ADDR + 0x00500000;
  uint32_t expected_write_bytes = (960 * 540 * 4);
  msgdma_write_stream_push(ctx->write_dma_desc, hidden_buffer_addr,
                           expected_write_bytes);

  printf("Write DMA (rx_dma_write) configured to catch %u bytes at 0x%08X "
         "(Hidden Buffer)\n",
         expected_write_bytes, hidden_buffer_addr);

  // ----------------------------------------------------
  // 3. Configure rx_dma_read (To push from DDR to RTL)
  // ----------------------------------------------------
  // -------------------------------------------------------------------------
  // MAGIC THROTTLING FIX (6008 Bytes)
  // The screen tears because MSGDMA bursts the entire 1.6MB file at once,
  // starving the HDMI Video DMA. We MUST throttle the MSGDMA transfer!
  // 1. Qsys 32-bit Data Adapter hangs if chunk is not a multiple of 4 bytes.
  // 2. RTL Header Stripper shifts pixels if chunk is not a multiple of 1502
  // bytes. The Lowest Common Multiple (LCM) of 4 and 1502 is EXACTLY 6008 bytes
  // (4 packets).
  // -------------------------------------------------------------------------
  uint32_t current_src_addr = FPGA_SHARED_DDR_PHYS_ADDR;
  uint32_t remaining_bytes = read_length_bytes;
  uint32_t MAGIC_CHUNK_SIZE = 6008; // 4 packets * 1502 bytes/packet
  int chunk_count = 0;

  printf("[INFO] Throttling MSGDMA to prevent DDR3 Starvation...\n");
  printf(
      "[INFO] Pushing %u bytes in chunks of 6008 bytes (4-packet blocks)...\n",
      (uint32_t)read_length_bytes);

  while (remaining_bytes > 0) {
    uint32_t chunk_size = (remaining_bytes > MAGIC_CHUNK_SIZE)
                              ? MAGIC_CHUNK_SIZE
                              : remaining_bytes;

    // Push exactly 4 packets to hardware
    msgdma_read_stream_push(ctx->read_dma_desc, current_src_addr, chunk_size);

    // Wait for the DMA transfer to physically finish this chunk
    while ((IORD_32DIRECT(ctx->read_dma_csr, MSGDMA_CSR_STATUS) & 0x01) != 0) {
    }

    // Sleep to yield AXI bridge / DDR3 bandwidth to the HDMI Video DMA reader
    usleep(1000);

    remaining_bytes -= chunk_size;
    current_src_addr += chunk_size;
    chunk_count++;
  }
  printf("[INFO] Finished streaming %d throttled blocks!\n", chunk_count);

  // Wait for DMA Write to finish (Busy bit 0)
  printf("[INFO] Waiting for Write DMA to finish...\n");
  while ((IORD_32DIRECT(ctx->write_dma_csr, MSGDMA_CSR_STATUS) & 0x01) != 0) {
    usleep(100);
  }

  // Wait for DMA Read to finish (Busy bit 0)
  printf("[INFO] Waiting for Read DMA to finish...\n");
  while ((IORD_32DIRECT(ctx->read_dma_csr, MSGDMA_CSR_STATUS) & 0x01) != 0) {
    usleep(100);
  }

  // Check for any errors
  uint32_t write_status = IORD_32DIRECT(ctx->write_dma_csr, MSGDMA_CSR_STATUS);
  uint32_t read_status = IORD_32DIRECT(ctx->read_dma_csr, MSGDMA_CSR_STATUS);

  printf("\n--- MSGDMA Completion Status ---\n");
  printf("Write DMA Status: 0x%08X (Error: %d)\n", write_status,
         (write_status >= 0x200));
  printf("Read DMA Status:  0x%08X (Error: %d)\n", read_status,
         (read_status >= 0x200));
  printf("--------------------------------\n\n");
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Usage: sudo %s <path_to_st2110_bin>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  struct app_context ctx;
  init_memory_mapping(&ctx);

  // --- Initialize HDMI Display to Primary Buffer ---
  // Before we start doing ANY DMA work, ensure the display is looking at a
  // clean buffer (0x20000000) so we don't see any tearing or garbage while
  // loading.
  enable_hdmi_display(&ctx); // This sets mode 8 and enables DMA
  IOWR_32DIRECT(ctx.hdmi_csr, REG_FRAME_PTR, FPGA_FRAME_BUFFER_PHYS_ADDR);
  printf("[INFO] HDMI initialized and pointing to Primary Buffer (0x%08X)\n",
         FPGA_FRAME_BUFFER_PHYS_ADDR);

  long file_size = load_binary_to_ddr(&ctx, argv[1]);

  printf("\n[DEBUG] file loaded to DDR3 Complete! Check the screen for any "
         "tearing that JUST occurred.\n");
  printf("Press [Enter] to start the DMA transfer...");
  getchar();

  start_dma_transfer(&ctx, file_size);

  // Clean Double Buffering: Update the frame pointer AFTER the DMA has
  // completely finished writing  //
  // ----------------------------------------------------------------------------------
  // INTERACTIVE DEBUG: Wait for user to acknowledge DMA completion
  // This allows the user to observe if the screen tore DURING the DMA transfer
  // to the hidden buffer. If it teared already, it means MSGDMA is starving the
  // Video DMA.
  // ----------------------------------------------------------------------------------
  printf("\n[DEBUG] DMA Transfer Complete! Check the screen for any tearing "
         "that JUST occurred.\n");
  printf("Press [Enter] to swap the HDMI Frame Pointer to the new image...");
  getchar();

  // --- Update HDMI Frame Pointer ---
  // We add a tiny delay to ensure all DDR3 write buffers from the DMA
  // are completely flushed before we point the Video DMA reader to it.
  usleep(5000); // 5ms sleep

  uint32_t hidden_buffer_addr = FPGA_FRAME_BUFFER_PHYS_ADDR + 0x00500000;
  printf("\n[INFO] Updating HDMI Frame Pointer to Hidden Buffer (0x%08X)...\n",
         hidden_buffer_addr);
  IOWR_32DIRECT(ctx.hdmi_csr, REG_FRAME_PTR, hidden_buffer_addr);

  // Wait 1 full frame (20ms) so the shadow pointer actually takes effect on
  // VSYNC and we don't exit/tear while the frame is switching.
  usleep(20000);

  printf(">>> ST2110 Frame is now on screen! (Frame Pointer Flipped to: "
         "0x%08X) <<<\n\n",
         hidden_buffer_addr);

  cleanup_system(&ctx);
  printf("Done.\n");

  return 0;
}
