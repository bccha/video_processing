#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define HW_REGS_BASE 0xFF200000
#define HW_REGS_SPAN 0x00200000
#define HW_REGS_MASK (HW_REGS_SPAN - 1)
#define HDMI_SYNC_GEN_OFFSET 0x2080

#ifndef IOWR_32DIRECT
#define IOWR_32DIRECT(base, offset, data)                                      \
  (*(volatile uint32_t *)((uint8_t *)(base) + (offset)) = (data))
#endif
#ifndef IORD_32DIRECT
#define IORD_32DIRECT(base, offset)                                            \
  (*(volatile uint32_t *)((uint8_t *)(base) + (offset)))
#endif

#define REG_PATTERN_MODE 0
#define REG_GLOBAL_CTRL (1 * 4)
#define REG_FRAME_PTR (6 * 4)

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: %s <phys_addr_hex>\n", argv[0]);
    printf("Example: %s 20000000\n", argv[0]);
    return 1;
  }

  uint32_t new_fb_addr = (uint32_t)strtoul(argv[1], NULL, 16);

  int fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (fd == -1) {
    perror("open /dev/mem");
    return 1;
  }

  void *virtual_base = mmap(NULL, HW_REGS_SPAN, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, HW_REGS_BASE);
  if (virtual_base == MAP_FAILED) {
    perror("mmap");
    close(fd);
    return 1;
  }

  volatile uint32_t *hdmi_csr =
      (volatile uint32_t *)((uint8_t *)virtual_base + HDMI_SYNC_GEN_OFFSET);

  printf("HPS: Setting Frame Pointer to 0x%08X\n", new_fb_addr);

  // 1. Ensure DMA Mode is enabled (Mode 8)
  IOWR_32DIRECT(hdmi_csr, REG_PATTERN_MODE, 8);
  // 2. Ensure Continuous DMA is enabled (Bit 1)
  uint32_t current_ctrl = IORD_32DIRECT(hdmi_csr, REG_GLOBAL_CTRL);
  IOWR_32DIRECT(hdmi_csr, REG_GLOBAL_CTRL, current_ctrl | 0x02);
  // 3. Update Frame Pointer
  IOWR_32DIRECT(hdmi_csr, REG_FRAME_PTR, new_fb_addr);

  printf("Done. Current Control Register: 0x%08X\n",
         IORD_32DIRECT(hdmi_csr, REG_GLOBAL_CTRL));

  munmap(virtual_base, HW_REGS_SPAN);
  close(fd);

  return 0;
}
