#include <fcntl.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * ==============================================================================
 * Hybrid ST 2110 Receiver - Phase 2: DDR Ring Buffer & mmap
 * ==============================================================================
 * Description:
 * This program extends the Phase 1 receiver by mapping a physical DDR address
 * range (outside Linux kernel space) to user space using mmap(). Incoming
 * packet chunks (1502 bytes) are directly copied into this Ring Buffer.
 * A dummy "Write Pointer" is updated to simulate notifying the FPGA's DMA.
 * ==============================================================================
 */

#define TARGET_ETH_IF "usb0"
#define BUFFER_SIZE 2048
#define HEADER_STRIP_SIZE 62

// --- Phase 2: Memory Mapping Constants ---
// Note: These addresses MUST match your actual Qsys/Linux memory map.
// For DE10-Nano, usually physical DDR 0x2000_0000 - 0x3FFF_FFFF is available.
#define FPGA_SHARED_DDR_PHYS_ADDR                                              \
  0x30000000 // Base physical address for Ring Buffer
#define RING_BUFFER_SIZE_BYTES (1920 * 1080 * 4) // E.g., Size for a few frames

// FPGA HPS2FPGA or LWH2F bridge address for the "Write Pointer" Register
// Assuming Lightweight HPS-to-FPGA bridge base is 0xFF200000
#define LWH2F_BASE 0xFF200000
#define LWH2F_SPAN 0x00200000
#define WRITE_POINTER_REG_OFFSET                                               \
  0x00000000 // Replace with actual Avalon-MM slave offset

int main(int argc, char *argv[]) {
  // --- 1. Memory Mapping (Phase 2) ---
  int fd_mem;
  void *virtual_ddr_base;
  void *virtual_lwh2f_base;
  volatile uint32_t *write_pointer_reg;

  printf("--- ST 2110 Receiver (Phase 2: Zero-Copy mmap) ---\n");

  // Open /dev/mem to access physical memory
  if ((fd_mem = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) {
    perror("Error opening /dev/mem (Must run as root/sudo)");
    exit(EXIT_FAILURE);
  }

  // Map the shared DDR Ring Buffer
  virtual_ddr_base =
      mmap(NULL, RING_BUFFER_SIZE_BYTES, (PROT_READ | PROT_WRITE), MAP_SHARED,
           fd_mem, (off_t)FPGA_SHARED_DDR_PHYS_ADDR);

  if (virtual_ddr_base == MAP_FAILED) {
    perror("Error mapping physical DDR memory");
    close(fd_mem);
    exit(EXIT_FAILURE);
  }
  // Map the LWH2F bridge for Avalon-MM register access (Write Pointer)
  virtual_lwh2f_base = mmap(NULL, LWH2F_SPAN, (PROT_READ | PROT_WRITE),
                            MAP_SHARED, fd_mem, (off_t)LWH2F_BASE);

  if (virtual_lwh2f_base == MAP_FAILED) {
    perror("Error mapping LWH2F bridge memory");
    munmap(virtual_ddr_base, RING_BUFFER_SIZE_BYTES);
    close(fd_mem);
    exit(EXIT_FAILURE);
  }
  write_pointer_reg =
      (uint32_t *)(virtual_lwh2f_base + WRITE_POINTER_REG_OFFSET);
  printf("Successfully mapped LWH2F Bridge. Write Pointer Reg at Virtual Addr "
         "%p\n\n",
         write_pointer_reg);

  // --- 2. Network Setup (Phase 1) ---
  int raw_sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
  if (raw_sock < 0) {
    perror("Socket failed");
    exit(EXIT_FAILURE);
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, TARGET_ETH_IF, IFNAMSIZ - 1);
  if (ioctl(raw_sock, SIOCGIFINDEX, &ifr) < 0) {
    perror("ioctl failed");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_ll sll;
  memset(&sll, 0, sizeof(sll));
  sll.sll_family = AF_PACKET;
  sll.sll_protocol = htons(ETH_P_IP);
  sll.sll_ifindex = ifr.ifr_ifindex;
  if (bind(raw_sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  printf("Listening on [%s] for video packets...\n", TARGET_ETH_IF);

  unsigned char buffer[BUFFER_SIZE];
  uint32_t current_ring_offset = 0;
  int packet_count = 0;

  // --- 3. Capture & mmap Copy Loop ---
  while (1) {
    ssize_t data_len = recvfrom(raw_sock, buffer, BUFFER_SIZE, 0, NULL, NULL);
    if (data_len < 0)
      break;

    // Filter Video Packets (MTU standard chunks)
    if (data_len > 1400 && data_len <= 1518) {
      packet_count++;

      // Phase 2 Core: "무지성 상하차" (Blindly copy to DDR)
      // Copy the entire packet (including the 62-byte header, as requested)
      // into the physical DDR memory mapped space.
      void *dest_addr = virtual_ddr_base + current_ring_offset;
      memcpy(dest_addr, buffer, data_len);

      // Advance the offset (wrapping around if needed)
      current_ring_offset += data_len;
      if (current_ring_offset + 1518 > RING_BUFFER_SIZE_BYTES) {
        current_ring_offset = 0; // Wrap around Ring Buffer
        // Update the Avalon-MM "Write Pointer" register for FPGA DMA to
        // track
        *write_pointer_reg = current_ring_offset;
      }

      // Print status every 1000 packets
      if (packet_count % 1000 == 0) {
        printf("[%d] Copied packet to DDR Virtual %p (Phys 0x%08X). Write Ptr: "
               "0x%08X\n",
               packet_count, dest_addr,
               FPGA_SHARED_DDR_PHYS_ADDR + (current_ring_offset - data_len),
               current_ring_offset);
      }
    }
  }

  // Cleanup
  close(raw_sock);
  munmap(virtual_lwh2f_base, LWH2F_SPAN); // Enabled
  munmap(virtual_ddr_base, RING_BUFFER_SIZE_BYTES);
  close(fd_mem);
  return 0;
}
