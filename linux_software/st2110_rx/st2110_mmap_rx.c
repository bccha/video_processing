#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "st2110_common.h"

// --- Network Constants (Specific to Receiver) ---
#define TARGET_ETH_IF "usb0"
#define BUFFER_SIZE 2048
#define HEADER_STRIP_SIZE 62
#define PAYLOAD_SIZE 1440
#define PACKETS_PER_FRAME 1080
#define PIXELS_PER_PACKET 480 // 1440 bytes / 3 bytes per pixel
#define FRAME_WIDTH 960
#define FRAME_HEIGHT 540
#define BYTES_PER_PIXEL 4 // Video DMA expects 32-bit words (RGB + Dummy)

// We need a specific pointer for the 0x20000000 frame buffer
void *virtual_frame_buffer_base;

// --- 2. Network Setup (Phase 1) ---
void init_network_socket(struct app_context *ctx) {
  ctx->raw_sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
  if (ctx->raw_sock < 0) {
    perror("Socket failed");
    exit(EXIT_FAILURE);
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, TARGET_ETH_IF, IFNAMSIZ - 1);
  if (ioctl(ctx->raw_sock, SIOCGIFINDEX, &ifr) < 0) {
    perror("ioctl failed");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_ll sll;
  memset(&sll, 0, sizeof(sll));
  sll.sll_family = AF_PACKET;
  sll.sll_protocol = htons(ETH_P_IP);
  sll.sll_ifindex = ifr.ifr_ifindex;
  if (bind(ctx->raw_sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  printf("Listening on [%s] for video packets...\n", TARGET_ETH_IF);
}

// Map the specific Frame Buffer region
void init_frame_buffer_map(struct app_context *ctx) {
  // Map 10MB to cover both ping-pong buffers (e.g., 0 MB and 5 MB offsets)
  size_t map_size = 10 * 1024 * 1024;
  virtual_frame_buffer_base =
      mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->fd_mem,
           FPGA_FRAME_BUFFER_PHYS_ADDR);
  if (virtual_frame_buffer_base == MAP_FAILED) {
    perror("Error mapping physical Frame Buffer memory");
    exit(EXIT_FAILURE);
  }
  printf("Mapped Frame Buffer (0x%08X) -> %p\n", FPGA_FRAME_BUFFER_PHYS_ADDR,
         virtual_frame_buffer_base);
}

// --- Software Alignment Wrapper (8-bit to 32-bit RGB) ---
// Takes a 1440-byte raw ST2110 payload and writes it directly to the mapped
// Frame Buffer in the 32-bit format expected by the HDMI Video DMA.
void write_payload_to_framebuffer(uint32_t *fb_dest, const uint8_t *payload,
                                  int num_pixels) {
  // To avoid Endianness issues between the ARM (Little Endian) and the
  // HDMI Video DMA (which expects the RTL's exact byte-lane layout),
  // we cast the destination to uint8_t* and write precisely byte-by-byte.
  // The RTL st2110_alignment_wrapper packs: {8'h00, Red, Green, Blue}
  // Which lands in DDR3 as byte sequence: [0x00, R, G, B]
  uint8_t *byte_dest = (uint8_t *)fb_dest;
  for (int i = 0; i < num_pixels; i++) {
    uint8_t r = payload[i * 3 + 0];
    uint8_t g = payload[i * 3 + 1];
    uint8_t b = payload[i * 3 + 2];

    byte_dest[i * 4 + 0] = 0x00; // Dummy Byte
    byte_dest[i * 4 + 1] = r;    // Red
    byte_dest[i * 4 + 2] = g;    // Green
    byte_dest[i * 4 + 3] = b;    // Blue
  }
}

// --- 3. Capture & Direct Write Loop ---
void capture_and_dma_loop(struct app_context *ctx) {
  unsigned char buffer[BUFFER_SIZE];
  int packet_count = 0;

  // Double Buffering State
  int display_buffer_idx = 0;
  const uint32_t buffer_offsets[2] = {0x00000000,
                                      0x00500000}; // 0 and 5MB offset

  // Point HDMI to Primary Buffer initially
  uint32_t current_display_addr =
      FPGA_FRAME_BUFFER_PHYS_ADDR + buffer_offsets[display_buffer_idx];
  *(ctx->hdmi_csr + (REG_FRAME_PTR / 4)) = current_display_addr;

  // We write to the *Hidden* Buffer
  int write_buffer_idx = 1;
  uint32_t current_write_offset =
      0; // Tracks pixel word offset within the hidden frame

  printf("Ready to receive ST2110 traffic (Direct Write Mode)...\n");

  while (1) {
    ssize_t data_len =
        recvfrom(ctx->raw_sock, buffer, BUFFER_SIZE, 0, NULL, NULL);

    if (data_len < 0) {
      perror("Error receiving packet");
      break;
    }

    // Process valid ST2110 Video Packets
    if (data_len > 1400 && data_len <= 1518) {

      // Calculate where to write in the mapped DDR memory
      uint32_t write_base_addr_offset = buffer_offsets[write_buffer_idx];
      uint32_t *dest_ptr = (uint32_t *)((uint8_t *)virtual_frame_buffer_base +
                                        write_base_addr_offset);

      // 1. Software Stripping: Skip 62-byte header
      const uint8_t *video_payload = buffer + HEADER_STRIP_SIZE;

      // 2. Alignment & Direct Write: Convert 1440 bytes to 480 32-bit words
      write_payload_to_framebuffer(dest_ptr + current_write_offset,
                                   video_payload, PIXELS_PER_PACKET);

      current_write_offset += PIXELS_PER_PACKET;
      packet_count++;

      // Check RTP Marker bit (Byte 43, bit 7) indicates EOP
      int is_eop = (buffer[43] & 0x80) != 0;

      // Frame Complete
      if (is_eop || packet_count >= PACKETS_PER_FRAME) {

        // 3. Swap Frame Pointers (Double Buffering)
        uint32_t new_display_addr =
            FPGA_FRAME_BUFFER_PHYS_ADDR + buffer_offsets[write_buffer_idx];
        *(ctx->hdmi_csr + (REG_FRAME_PTR / 4)) = new_display_addr;

        printf("\r[INFO] Frame Finished! Swapped HDMI pointer to Buffer %d "
               "(0x%08X).",
               write_buffer_idx, new_display_addr);
        fflush(stdout);

        // Toggle buffers for next frame
        display_buffer_idx = write_buffer_idx;
        write_buffer_idx = 1 - write_buffer_idx;

        // Reset tracking for the next frame
        packet_count = 0;
        current_write_offset = 0;
      }
    }
  }
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================
int main(int argc, char *argv[]) {
  printf("--- ST 2110 Direct Mmap Receiver ---\n");
  printf("Architecture: Network -> ARM (Software Strip) -> Direct 0x20000000 "
         "Mmap -> "
         "HDMI DMA\n");

  struct app_context ctx;

  init_memory_mapping(&ctx);
  init_frame_buffer_map(&ctx);
  // We no longer need to initialize DMA engines!
  init_network_socket(&ctx);

  capture_and_dma_loop(&ctx);

  cleanup_system(&ctx);

  return 0;
}
