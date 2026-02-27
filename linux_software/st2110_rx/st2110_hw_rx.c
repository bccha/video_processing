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
#include <sys/socket.h>
#include <unistd.h>

#include "st2110_common.h"

// --- Constants ---
#define TARGET_ETH_IF "usb0"
#define BUFFER_SIZE 2048
#define HEADER_STRIP_SIZE 62
#define PAYLOAD_SIZE 1440
#define PACKETS_PER_FRAME 1080

// Ring Buffer in sdram1 (Assumes sdram1 is configured to pass to 0x30000000
// physical)
#define ST2110_RING_BUFFER_PHYS_ADDR 0x30000000
#define MAX_CHUNK_PACKETS 4 // We will push 4 packets per DMA burst

// --- Network Initialization ---
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

  printf("Listening on [%s] for raw ST2110 packets...\n", TARGET_ETH_IF);
}

// --- Frame Processing Loop (Hardware Accelerated) ---
void hw_capture_and_dma_loop(struct app_context *ctx) {
  unsigned char buffer[BUFFER_SIZE];
  int packet_count = 0;

  // Frame Buffer offsets in sdram0 (0x20000000 base)
  int display_buffer_idx = 0;
  const uint32_t buffer_offsets[2] = {0x00000000, 0x00500000};

  uint32_t current_display_addr =
      FPGA_FRAME_BUFFER_PHYS_ADDR + buffer_offsets[display_buffer_idx];
  *(ctx->hdmi_csr + (REG_FRAME_PTR / 4)) = current_display_addr;

  int write_buffer_idx = 1;
  uint32_t ring_buffer_offset = 0;

  // Initialize DMA
  msgdma_init(ctx->write_dma_csr);
  msgdma_init(ctx->read_dma_csr);

  // Set up the Sink (Write) DMA to catch the first frame
  uint32_t current_write_base_addr =
      FPGA_FRAME_BUFFER_PHYS_ADDR + buffer_offsets[write_buffer_idx];
  uint32_t frame_byte_size = (960 * 540 * 4); // 32-bits per pixel from RTL

  msgdma_write_stream_push(ctx->write_dma_desc, current_write_base_addr,
                           frame_byte_size);
  printf("\n[HW RX] Write DMA armed. Catching processed pixels at 0x%08X\n",
         current_write_base_addr);

  printf("Waiting for network packets...\n");

  while (1) {
    ssize_t data_len =
        recvfrom(ctx->raw_sock, buffer, BUFFER_SIZE, 0, NULL, NULL);

    if (data_len < 0) {
      perror("Error receiving packet");
      break;
    }

    if (data_len > 1400 && data_len <= 1518) {
      // 1. Write the Raw Packet directly to the temporary Ring Buffer in ARM
      // RAM In a real zero-copy driver, the NIC would DMA directly to DDR, but
      // here we just copy to our mapped DDR window (sdram1)
      uint32_t *dest_ptr =
          (uint32_t *)((uint8_t *)ctx->virtual_ddr_base + ring_buffer_offset);
      memcpy(dest_ptr, buffer, data_len);

      uint32_t packet_phys_addr =
          FPGA_SHARED_DDR_PHYS_ADDR + ring_buffer_offset;
      ring_buffer_offset += data_len;
      if (ring_buffer_offset > RING_BUFFER_SIZE_BYTES - BUFFER_SIZE) {
        ring_buffer_offset = 0; // Wrap around
      }

      // 2. Instruct mSGDMA to read the packet and push into RTL
      // We push exactly 1 packet size (Header + Data). RTL will slice off the
      // 62 bytes.
      msgdma_read_stream_push(ctx->read_dma_desc, packet_phys_addr, data_len);

      packet_count++;

      // Check EOP
      int is_eop = (buffer[43] & 0x80) != 0;

      if (is_eop || packet_count >= PACKETS_PER_FRAME) {

        // Wait for current write frame to finish
        // (In a perfect pipeline, checking the status bit is sufficient)
        while ((IORD_32DIRECT(ctx->write_dma_csr, MSGDMA_CSR_STATUS) & 0x01) !=
               0) {
          // Busy wait
        }

        // 3. Swap HDMI Pointers
        uint32_t new_display_addr =
            FPGA_FRAME_BUFFER_PHYS_ADDR + buffer_offsets[write_buffer_idx];
        *(ctx->hdmi_csr + (REG_FRAME_PTR / 4)) = new_display_addr;

        printf("\r[HW RX] Frame %d Finished! HDMI -> 0x%08X",
               (packet_count >= PACKETS_PER_FRAME), new_display_addr);
        fflush(stdout);

        // Toggle Buffers
        display_buffer_idx = write_buffer_idx;
        write_buffer_idx = 1 - write_buffer_idx;
        packet_count = 0;

        // Arm DMA Write for the next frame
        current_write_base_addr =
            FPGA_FRAME_BUFFER_PHYS_ADDR + buffer_offsets[write_buffer_idx];
        msgdma_write_stream_push(ctx->write_dma_desc, current_write_base_addr,
                                 frame_byte_size);
      }
    }
  }
}

int main(int argc, char *argv[]) {
  printf("--- ST 2110 Hardware Accelerated Receiver (Dual Port Mode) ---\n");

  struct app_context ctx;

  init_memory_mapping(&ctx);
  // Ensure HDMI starts enabled
  enable_hdmi_display(&ctx);

  init_network_socket(&ctx);

  hw_capture_and_dma_loop(&ctx);

  cleanup_system(&ctx);

  return 0;
}
