#define _GNU_SOURCE
#include "st2110_common.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

// --- Network Constants (Specific to Receiver) ---
#define TARGET_ETH_IF "eth0"
#define BUFFER_SIZE 2048
#define UDP_PAYLOAD_SIZE 1460    // 12 (RTP) + 8 (SRD) + 1440 (Video)
#define UDP_HEADER_STRIP_SIZE 20 // 12 (RTP) + 8 (SRD)
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
  ctx->raw_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (ctx->raw_sock < 0) {
    perror("UDP Socket failed");
    exit(EXIT_FAILURE);
  }

  // Force the kernel to allow massive Rx buffers (requires root)
  // 256MB max buffer override for Gigabit ST2110 video streams
  system("sysctl -w net.core.rmem_max=268435456 > /dev/null 2>&1");

  // Request 256MB socket buffer
  int rcvbuf_size = 256 * 1024 * 1024;
  if (setsockopt(ctx->raw_sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size,
                 sizeof(rcvbuf_size)) < 0) {
    perror("Failed to set SO_RCVBUF");
  }

  // Verify actual socket buffer size granted by the OS
  int actual_rcvbuf = 0;
  socklen_t optlen = sizeof(actual_rcvbuf);
  getsockopt(ctx->raw_sock, SOL_SOCKET, SO_RCVBUF, &actual_rcvbuf, &optlen);
  printf("[NETWORK] Requested %d bytes, OS granted %d bytes for UDP Payload "
         "Buffer\n",
         rcvbuf_size, actual_rcvbuf);

  // Remove the old 1ms timeout! Since recvfrom is now in its own dedicated
  // thread, it can safely block indefinitely until a packet arrives.
  // This completely eliminates context-switch overhead caused by timeouts.
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(5000);

  if (bind(ctx->raw_sock, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {
    perror("UDP bind failed");
    exit(EXIT_FAILURE);
  }

  printf("Listening on UDP port 5000 for video packets...\n");
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

  // Clear 10MB Frame Buffer to Black (0x00000000) so no green garbage appears
  // before the first ST2110 frames arrive.
  printf("Clearing Frame Buffers to Black...\n");
  memset(virtual_frame_buffer_base, 0, map_size);
}

// --- Software Alignment Wrapper (Removed) ---
// We no longer software-copy bits. The MSGDMA hardware and the RTL Alignment
// Wrapper handles the 1440-byte to 480 32-bit pixel words conversion natively.

// --- 3. Network Receive Thread ---
// This thread constantly pulls UDP packets from the OS socket buffer and places
// them directly into FPGA DDR memory. It is completely uncoupled from the DMA,
// so it never blocks and never drops packets.
void *network_rx_thread_func(void *arg) {
  struct app_context *ctx = (struct app_context *)arg;
  unsigned char buffer[BUFFER_SIZE];
  uint32_t aligned_offset_bytes = 1504;
  uint32_t HEADER_BYTES = 62;
  uint32_t FRAME_PAYLOAD_BYTES = 1440;

  printf("Network Rx Thread started (Polling UDP port 5000)...\n");

  while (1) {
    int data_len = recvfrom(ctx->raw_sock, buffer, BUFFER_SIZE, 0, NULL, NULL);

    if (data_len < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      continue;
    } else if (data_len < 0) {
      perror("Error receiving packet");
      break;
    }

    if (data_len == UDP_PAYLOAD_SIZE) {
      // Parse RTP and SRD Headers from the payload
      uint16_t row = (buffer[16] << 8) | buffer[17];
      uint16_t offset = (buffer[18] << 8) | buffer[19];

      // Calculate absolute packet index (0 to 1079) based on SRD
      int packet_index = (row * 2) + (offset == 1440 ? 1 : 0);

      // Calculate the target position for this packet directly in FPGA DDR
      // memory
      uint32_t packet_offset = packet_index * aligned_offset_bytes;
      uint8_t *pkt_base = (uint8_t *)ctx->virtual_ddr_base + packet_offset;

      // 1. Pre-Zero out the 62 byte ST2110 mockup header space
      memset(pkt_base, 0, HEADER_BYTES);

      // 2. Copy the actual 1440 video bytes into the payload section
      const uint8_t *video_payload = buffer + UDP_HEADER_STRIP_SIZE;
      memcpy(pkt_base + HEADER_BYTES, video_payload, FRAME_PAYLOAD_BYTES);

      // Check RTP Marker bit for EOP
      int is_eop = (buffer[1] & 0x80) != 0;
      if (is_eop) {
        pkt_base[43] = 0x80;
      }
    }
  }
  return NULL;
}

// --- 4. DMA Trigger & V-Sync Swap Loop ---
void capture_and_dma_loop(struct app_context *ctx, double target_fps) {
  // Double Buffering State for the HDMI output
  int display_buffer_idx = 0;
  const uint32_t buffer_offsets[2] = {0x00000000,
                                      0x00500000}; // 0 and 5MB offset

  // Point HDMI to Primary Buffer initially
  uint32_t current_display_addr =
      FPGA_FRAME_BUFFER_PHYS_ADDR + buffer_offsets[display_buffer_idx];
  *(ctx->hdmi_csr + (REG_FRAME_PTR / 4)) = current_display_addr;

  // We write to the *Hidden* Buffer via DMA
  int write_buffer_idx = 1;

  // Initialize MSGDMA
  msgdma_init(ctx->read_dma_csr);
  msgdma_init(ctx->write_dma_csr);

  printf("Ready to push ST2110 frames (Hardware MSGDMA Mode @ %.2f FPS)...\n",
         target_fps);

  uint32_t HEADER_BYTES = 62;
  uint32_t FRAME_PAYLOAD_BYTES = 1440;
  uint32_t FRAME_TOTAL_RX_BYTES = HEADER_BYTES + FRAME_PAYLOAD_BYTES; // 1502
  uint32_t FRAME_TOTAL_TX_BYTES = 1920;
  uint32_t aligned_offset_bytes = 1504;

  struct timeval fps_start, fps_end, start_play_time;
  int fps_frame_count = 0;
  gettimeofday(&fps_start, NULL);
  gettimeofday(&start_play_time, NULL);
  double target_accum_time = 0.0;

  msgdma_reset_pipeline(ctx->write_dma_csr, ctx->read_dma_csr);

  while (1) {

    // 1. FPS Throttling (Absolute Software Timer)
    // We use elapsed time since start so we NEVER drift or lose pace over time.
    struct timeval loop_now;
    gettimeofday(&loop_now, NULL);
    double elapsed_since_start =
        (loop_now.tv_sec - start_play_time.tv_sec) +
        (loop_now.tv_usec - start_play_time.tv_usec) / 1000000.0;

    if (elapsed_since_start >= target_accum_time) {
      target_accum_time += (1.0 / target_fps);

      uint32_t current_write_base =
          FPGA_FRAME_BUFFER_PHYS_ADDR + buffer_offsets[write_buffer_idx];

      struct timeval dma_start, dma_end;
      gettimeofday(&dma_start, NULL);

      int timeout_error = msgdma_transmit_burst_pipeline(
          ctx->write_dma_csr, ctx->write_dma_desc, ctx->read_dma_csr,
          ctx->read_dma_desc, FPGA_SHARED_DDR_PHYS_ADDR, current_write_base,
          PACKETS_PER_FRAME, aligned_offset_bytes, FRAME_TOTAL_TX_BYTES,
          FRAME_TOTAL_RX_BYTES);

      gettimeofday(&dma_end, NULL);
      double dma_duration = (dma_end.tv_sec - dma_start.tv_sec) +
                            (dma_end.tv_usec - dma_start.tv_usec) / 1000000.0;

      if (timeout_error) {
        uint32_t wr_status =
            IORD_32DIRECT(ctx->write_dma_csr, MSGDMA_CSR_STATUS);
        uint32_t rd_status =
            IORD_32DIRECT(ctx->read_dma_csr, MSGDMA_CSR_STATUS);
        printf("ERROR: DMA Burst Timeout!\n");
        printf("Write DMA CSR Status: 0x%08X\n", wr_status);
        printf("Read DMA CSR Status:  0x%08X\n", rd_status);
      } else {
        // 2. Wait for the Next V-Sync Edge FIRST
        // This ensures the hardware is ready to latch the new pointer.
        // Doing this AFTER the DMA overhead guarantees perfectly smooth
        // playback aligned with the monitor without dropping software timer
        // accuracy.
        static uint32_t last_vs_state = 0;
        while (1) {
          uint32_t status = *(ctx->hdmi_csr + (REG_GLOBAL_CTRL / 4));
          uint32_t current_vs_state = (status >> 29) & 0x01;
          if (current_vs_state != last_vs_state) {
            last_vs_state = current_vs_state;
            break; // V-Sync edge detected
          }
          usleep(1);
        }

        // 3. Swap Frame Pointers (Double Buffering)
        // The HDMI will safely latch this pointer at the NEXT V-Sync edge
        *(ctx->hdmi_csr + (REG_FRAME_PTR / 4)) = current_write_base;

        fps_frame_count++;
        if (fps_frame_count >= 60) {
          gettimeofday(&fps_end, NULL);
          double elapsed = (fps_end.tv_sec - fps_start.tv_sec) +
                           (fps_end.tv_usec - fps_start.tv_usec) / 1000000.0;
          double fps = fps_frame_count / elapsed;

          printf("[INFO] Wall-Clock FPS: %.2f | Last DMA copy: %.2f ms | "
                 "Swapped HDMI pointer to Buffer %d "
                 "(0x%08X).\n",
                 fps, dma_duration * 1000.0, write_buffer_idx,
                 current_write_base);
          fflush(stdout);

          fps_frame_count = 0;
          gettimeofday(&fps_start, NULL);
        }

        // Toggle buffers for next frame
        int temp = write_buffer_idx;
        write_buffer_idx = display_buffer_idx;
        display_buffer_idx = temp;
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

  double target_fps = 20.0;
  if (argc > 1) {
    target_fps = atof(argv[1]);
  }
  printf("Target DMA Framerate: %.2f FPS\n", target_fps);

  struct app_context ctx;

  init_memory_mapping(&ctx);
  init_frame_buffer_map(&ctx);
  // We no longer need to initialize DMA engines!
  init_network_socket(&ctx);

  // Launch Network Receive Thread (Detached)
  pthread_t rx_tid;
  if (pthread_create(&rx_tid, NULL, network_rx_thread_func, &ctx) != 0) {
    perror("Failed to spawn network Rx thread");
    exit(EXIT_FAILURE);
  }

  // Double buffering loop blocks main thread parsing the framerate
  // target_fps
  capture_and_dma_loop(&ctx, target_fps);

  close(ctx.raw_sock);
  cleanup_system(&ctx);

  pthread_cancel(rx_tid);
  pthread_join(rx_tid, NULL);

  return 0;
}
