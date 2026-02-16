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
#define RESERVED_RAM_SIZE (512 * 1024 * 1024) // 512 MB (Full reserved space)
#define PHY_ADDR_FRAME_BUF_BASE 0x20000000    // Start of upper 512MB

// FPGA CSR Addresses (HPS Lightweight Bridge)
#define CSR_BASE_PHY 0xFF200000
#define CSR_SPAN 0x00100000 // 1MB
#define HDMI_SYNC_GEN_OFFSET 0x00010100

// Register Offsets (Match hdmi_control.h)
#define REG_PATTERN_MODE 0
#define REG_GLOBAL_CTRL (1 * 4)
#define REG_FRAME_PTR (6 * 4)

void *map_physical_memory(off_t base, size_t span) {
  int fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (fd == -1) {
    perror("Error opening /dev/mem");
    exit(EXIT_FAILURE);
  }
  void *virtual_base =
      mmap(NULL, span, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base);
  if (virtual_base == MAP_FAILED) {
    perror("Error mapping memory");
    close(fd);
    exit(EXIT_FAILURE);
  }
  close(fd);
  return virtual_base;
}

int main(int argc, char **argv) {
  printf("DE10-Nano RAM Preload Video Player\n");
  printf("Mode: Store-and-Forward (Load -> Play)\n");

  if (argc < 2) {
    printf("Usage: %s <video_file.bin | -> [fps]\n", argv[0]);
    printf("Use '-' to read from stdin (e.g. via SSH)\n");
    printf("Default FPS: 60\n");
    return 1;
  }

  char *input_source = argv[1];
  int target_fps = (argc >= 3) ? atoi(argv[2]) : 60;
  if (target_fps <= 0)
    target_fps = 60;

  FILE *fp;
  // ... [Open Input logic remains same]
  if (strcmp(input_source, "-") == 0) {
    fp = stdin;
    printf("Input: Standard Input (Streaming...)\n");
  } else {
    fp = fopen(input_source, "rb");
    if (!fp) {
      perror("Error opening video file");
      return -1;
    }
    printf("Input: File (%s)\n", input_source);
  }

  // 2. Map Memory
  printf("Mapping 512MB Reserved DDR3 at 0x%08X...\n", PHY_ADDR_FRAME_BUF_BASE);
  uint8_t *ram_base = (uint8_t *)map_physical_memory(PHY_ADDR_FRAME_BUF_BASE,
                                                     RESERVED_RAM_SIZE);

  void *csr_base = map_physical_memory(CSR_BASE_PHY, CSR_SPAN);
  volatile uint32_t *hdmi_csr =
      (volatile uint32_t *)((uint8_t *)csr_base + HDMI_SYNC_GEN_OFFSET);

  // 3. Load Phase
  printf(">>> START LOADING (Please Wait) <<<\n");
  size_t total_loaded = 0;
  size_t n_read;
  size_t chunk_size = 1024 * 1024; // 1MB chunks for reporting

  time_t start_time = time(NULL);

  while (total_loaded < RESERVED_RAM_SIZE) {
    size_t to_read = RESERVED_RAM_SIZE - total_loaded;
    if (to_read > chunk_size)
      to_read = chunk_size;

    n_read = fread(ram_base + total_loaded, 1, to_read, fp);
    if (n_read <= 0)
      break; // EOF or Error

    total_loaded += n_read;

    // Progress Bar (every 10MB)
    if (total_loaded % (10 * 1024 * 1024) == 0) {
      printf("\rLoaded: %lu MB", total_loaded / (1024 * 1024));
      fflush(stdout);
    }
  }

  printf("\rLoaded: %lu MB (Complete)\n", total_loaded / (1024 * 1024));
  time_t end_time = time(NULL);
  printf("Time taken: %ld seconds\n", end_time - start_time);

  if (fp != stdin)
    fclose(fp);

  size_t frame_count = total_loaded / FRAME_SIZE;
  printf("Total Frames: %lu (Target FPS: %d, approx %.1f sec)\n", frame_count,
         target_fps, (float)frame_count / target_fps);

  if (frame_count == 0) {
    printf("Error: No data loaded!\n");
    return -1;
  }

  // 4. Play Phase
  printf(">>> START PLAYBACK (%d fps) <<<\n", target_fps);
  printf("Setting HDMI Sync Gen to DMA Mode 8 & Enabling DMA...\n");

  // Set Mode 8 (DMA Stream)
  *(hdmi_csr + (REG_PATTERN_MODE / 4)) = 8;
  // Enable Continuous DMA (Bit 1)
  *(hdmi_csr + (REG_GLOBAL_CTRL / 4)) = 0x02;

  printf("Press Ctrl+C to stop.\n");

  struct timespec next_frame;
  clock_gettime(CLOCK_MONOTONIC, &next_frame);

  long long frame_interval_ns = 1000000000LL / target_fps;
  size_t current_frame = 0;

  while (1) {
    uint32_t frame_phy_addr =
        PHY_ADDR_FRAME_BUF_BASE + (current_frame * FRAME_SIZE);

    // Update Hardware Pointer
    *(hdmi_csr + (REG_FRAME_PTR / 4)) = frame_phy_addr;

    // Calculate next frame time
    next_frame.tv_nsec += frame_interval_ns;
    while (next_frame.tv_nsec >= 1000000000LL) {
      next_frame.tv_sec++;
      next_frame.tv_nsec -= 1000000000LL;
    }

    // Precise sleep until next frame
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_frame, NULL);

    // Loop
    current_frame++;
    if (current_frame >= frame_count) {
      current_frame = 0; // Loop back to start
    }
  }

  return 0;
}
