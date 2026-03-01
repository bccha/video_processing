#define _POSIX_C_SOURCE 199309L
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

// Configuration
#define WIDTH 960
#define HEIGHT 540
#define BPP 4 // Bytes per pixel (32-bit RGBA)
#define FRAME_SIZE (WIDTH * HEIGHT * BPP)

// Memory Limits
#define PHY_ADDR_FRAME_BUF_BASE 0x20000000 // Start of upper 512MB
#define PHY_ADDR_FB0 PHY_ADDR_FRAME_BUF_BASE
#define PHY_ADDR_FB1                                                           \
  (PHY_ADDR_FRAME_BUF_BASE + 0x00200000) // 2MB Offset (Aligned to 2MB)

// FPGA CSR Addresses (HPS Lightweight Bridge)
#define CSR_BASE_PHY 0xFF200000
#define CSR_SPAN 0x00100000 // 1MB
#define HDMI_SYNC_GEN_OFFSET 0x2080

#ifndef IOWR_32DIRECT
#define IOWR_32DIRECT(base, offset, data)                                      \
  (*(volatile uint32_t *)((uint8_t *)(base) + (offset)) = (data))
#endif
#ifndef IORD_32DIRECT
#define IORD_32DIRECT(base, offset)                                            \
  (*(volatile uint32_t *)((uint8_t *)(base) + (offset)))
#endif

// Register Offsets
#define REG_PATTERN_MODE 0
#define REG_GLOBAL_CTRL (1 * 4)
#define REG_FRAME_PTR (6 * 4)

void *map_physical_memory(int fd, off_t base, size_t span) {
  void *virtual_base =
      mmap(NULL, span, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base);
  if (virtual_base == MAP_FAILED) {
    perror("Error mapping memory");
    exit(EXIT_FAILURE);
  }
  return virtual_base;
}

int main(int argc, char **argv) {
  printf("DE10-Nano HDMI Double-Buffered Streamer\n");
  printf("Mode: Real-time Pipe (stdin -> DB -> Display)\n");

  int fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (fd == -1) {
    perror("Error opening /dev/mem");
    exit(EXIT_FAILURE);
  }

  // 1. Map CSR and Frame Buffers
  void *csr_base_virt = map_physical_memory(fd, CSR_BASE_PHY, CSR_SPAN);
  volatile uint32_t *hdmi_csr =
      (volatile uint32_t *)((uint8_t *)csr_base_virt + HDMI_SYNC_GEN_OFFSET);

  // Map only the first 16MB of FB area to cover two frames easily
  uint8_t *fb_base_virt =
      (uint8_t *)map_physical_memory(fd, PHY_ADDR_FRAME_BUF_BASE, 0x01000000);
  uint8_t *fb0_virt = fb_base_virt;
  uint8_t *fb1_virt = fb_base_virt + 0x00200000;

  // 2. Hardware Initialization
  printf("Initializing Hardware (Mode 8, Continuous DMA)...\n");
  IOWR_32DIRECT(hdmi_csr, REG_PATTERN_MODE, 8); // DMA Mode

  // To match |= 0x02, we read first, modify, then write
  uint32_t current_ctrl = IORD_32DIRECT(hdmi_csr, REG_GLOBAL_CTRL);
  IOWR_32DIRECT(hdmi_csr, REG_GLOBAL_CTRL,
                current_ctrl | 0x02); // Enable Continuous

  // 3. Streaming Loop
  printf(">>> START STREAMING <<<\n");
  printf("Pipe FFmpeg output here: ffmpeg ... -f rawvideo - | ./%s\n", argv[0]);

  int active_buffer = 0;
  size_t frame_count = 0;

  while (1) {
    uint8_t *write_ptr = (active_buffer == 0) ? fb0_virt : fb1_virt;
    uint32_t phys_ptr = (active_buffer == 0) ? PHY_ADDR_FB0 : PHY_ADDR_FB1;

    // Read full frame from stdin
    size_t total_read = 0;
    while (total_read < FRAME_SIZE) {
      ssize_t n =
          read(STDIN_FILENO, write_ptr + total_read, FRAME_SIZE - total_read);
      if (n <= 0) {
        printf("\nStream ended or error. Read %zu bytes.\n", total_read);
        goto cleanup;
      }
      total_read += n;
    }

    // Switch Hardware to this buffer
    IOWR_32DIRECT(hdmi_csr, REG_FRAME_PTR, phys_ptr);

    // Toggle buffer for next frame
    active_buffer = (active_buffer == 0) ? 1 : 0;
    frame_count++;

    if (frame_count % 30 == 0) {
      printf("\rStreamed: %zu frames", frame_count);
      fflush(stdout);
    }
  }

cleanup:
  printf("Done.\n");
  munmap(csr_base_virt, CSR_SPAN);
  munmap(fb_base_virt, 0x01000000);
  close(fd);
  return 0;
}