#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

struct termios orig_termios;

void reset_terminal_mode() { tcsetattr(0, TCSANOW, &orig_termios); }

void set_conio_terminal_mode() {
  struct termios new_termios;

  // Save old settings
  tcgetattr(0, &orig_termios);
  atexit(reset_terminal_mode); // Ensure reset on exit

  // Apply raw mode
  new_termios = orig_termios;
  new_termios.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(0, TCSANOW, &new_termios);
}

// Non-blocking keyboard hit check using select
int kbhit() {
  struct timeval tv = {0L, 0L};
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(0, &fds);
  return select(1, &fds, NULL, NULL, &tv) > 0;
}

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
  // Apply raw terminal mode for spacebar capture
  set_conio_terminal_mode();

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

  struct timespec start_play_time;
  clock_gettime(CLOCK_MONOTONIC, &start_play_time);

  long long frame_interval_ns = 1000000000LL / target_fps;
  size_t current_frame = 0;
  int is_paused = 0;

  printf("Playback Controls:\n");
  printf("  [S] Pause/Resume\n");
  printf("  [Ctrl+C] Quit\n");

  while (1) {
    // Handle Keyboard Input
    if (kbhit()) {
      char c = getchar();
      if (c == 's') {
        is_paused = !is_paused;
        if (is_paused) {
          printf("\r[PAUSED] Press S to resume...       ");
          fflush(stdout);
        } else {
          printf("\r[PLAYING] %d fps                        \n", target_fps);
          // Reset timer so we don't fast-forward after unpausing
          clock_gettime(CLOCK_MONOTONIC, &start_play_time);
        }
      }
    }

    if (is_paused) {
      usleep(10000); // Sleep 10ms to save CPU
      continue;
    }

    // 1. FPS Throttling (Wait for target frame time)
    struct timespec expected_time;
    long long total_ns = current_frame * frame_interval_ns;

    expected_time.tv_sec = start_play_time.tv_sec + (total_ns / 1000000000LL);
    expected_time.tv_nsec = start_play_time.tv_nsec + (total_ns % 1000000000LL);
    if (expected_time.tv_nsec >= 1000000000LL) {
      expected_time.tv_sec++;
      expected_time.tv_nsec -= 1000000000LL;
    }

    struct timespec now;
    do {
      clock_gettime(CLOCK_MONOTONIC, &now);
      if (now.tv_sec < expected_time.tv_sec ||
          (now.tv_sec == expected_time.tv_sec &&
           now.tv_nsec < expected_time.tv_nsec)) {
        usleep(500); // Yield CPU
      }
    } while (now.tv_sec < expected_time.tv_sec ||
             (now.tv_sec == expected_time.tv_sec &&
              now.tv_nsec < expected_time.tv_nsec));

    // 2. Wait for the Next V-Sync Edge FIRST
    // This ensures we are inside the active frame or just started the blanking
    // period. By waiting first, we synchronize our software loop precisely with
    // the hardware.
    static uint32_t last_vs_state = 0;
    while (1) {
      uint32_t status = *(hdmi_csr + (REG_GLOBAL_CTRL / 4));
      uint32_t current_vs_state = (status >> 29) & 0x01;
      if (current_vs_state != last_vs_state) {
        last_vs_state = current_vs_state;
        break; // V-Sync edge detected
      }
      // usleep(1) instead of 100 to catch the edge more precisely and avoid
      // jitter
      usleep(1);
    }

    // 3. Update the Frame Pointer for the NEXT frame
    // Writing this now guarantees reg_frame_ptr is absolutely stable before the
    // hardware attempts to latch it into shadow_ptr at the *next* V-Sync edge.
    // This eliminates the race condition where the CPU writes the address at
    // the exact moment the DMA is copying it.
    uint32_t frame_phy_addr =
        PHY_ADDR_FRAME_BUF_BASE + (current_frame * FRAME_SIZE);
    *(hdmi_csr + (REG_FRAME_PTR / 4)) = frame_phy_addr;

    // 4. Advance Frame Logic
    current_frame++;
    if (current_frame >= frame_count) {
      current_frame = 0; // Loop back to start
      clock_gettime(CLOCK_MONOTONIC,
                    &start_play_time); // Reset time for next loop
    }
  }

  return 0;
}