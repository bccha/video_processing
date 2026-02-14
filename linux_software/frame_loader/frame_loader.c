#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define HW_REGS_BASE (0x00000000)
#define HW_REGS_SPAN (0x40000000)
#define HW_REGS_MASK (HW_REGS_SPAN - 1)

#define FRAME_BUFFER_BASE 0x30000000
#define FRAME_WIDTH 1280
#define FRAME_HEIGHT 720
#define FRAME_SIZE (FRAME_WIDTH * FRAME_HEIGHT * 4)

int main(int argc, char **argv) {
  void *virtual_base;
  int fd;
  uint32_t *frame_ptr;

  if (argc < 2) {
    printf("Usage: %s <raw_image_file>\n", argv[0]);
    printf("Example: %s test.raw\n", argv[0]);
    return 1;
  }

  // Open /dev/mem
  if ((fd = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) {
    perror("Error: could not open \"/dev/mem\"");
    return 1;
  }

  // Memory map the physical address
  virtual_base = mmap(NULL, HW_REGS_SPAN, (PROT_READ | PROT_WRITE), MAP_SHARED,
                      fd, HW_REGS_BASE);
  if (virtual_base == MAP_FAILED) {
    perror("Error: mmap() failed");
    close(fd);
    return 1;
  }

  // Get pointer to the frame buffer
  frame_ptr = (uint32_t *)((uint8_t *)virtual_base + FRAME_BUFFER_BASE);

  // load file
  FILE *file = fopen(argv[1], "rb");
  if (!file) {
    perror("Error: could not open image file");
    munmap(virtual_base, HW_REGS_SPAN);
    close(fd);
    return 1;
  }

  printf("Loading %s to Physical Address 0x%08X...\n", argv[1],
         FRAME_BUFFER_BASE);
  size_t read_bytes = fread(frame_ptr, 1, FRAME_SIZE, file);
  printf("Successfully loaded %zu bytes.\n", read_bytes);

  fclose(file);

  // Clean up
  if (munmap(virtual_base, HW_REGS_SPAN) != 0) {
    perror("Error: munmap() failed");
    close(fd);
    return 1;
  }

  close(fd);
  return 0;
}
