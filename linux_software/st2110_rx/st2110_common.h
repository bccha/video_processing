#ifndef ST2110_COMMON_H
#define ST2110_COMMON_H

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

// --- Memory Map Constants (MUST Match Qsys / system.h) ---
#define FPGA_SHARED_DDR_PHYS_ADDR                                              \
  0x21000000 // Base physical address for Ring/Tx Buffer
#define RING_BUFFER_SIZE_BYTES (1920 * 1080 * 4) // E.g., Size for a few frames

// FPGA LWH2F bridge address for DMA Control
#define LWH2F_BASE 0xFF200000
#define LWH2F_SPAN 0x00200000

// MSGDMA RX Read (Memory -> Streaming)
#define RX_DMA_READ_CSR_OFFSET 0x20320
#define RX_DMA_READ_DESCRIPTOR_SLAVE_OFFSET 0x20340

// MSGDMA RX Write (Streaming -> Memory)
#define RX_DMA_WRITE_CSR_OFFSET 0x20360
#define RX_DMA_WRITE_DESCRIPTOR_SLAVE_OFFSET 0x20380

// Display Frame Buffer (RTL Output Destination)
#define FPGA_FRAME_BUFFER_PHYS_ADDR 0x20000000

// HDMI Sync Gen CSR Offset
#define HDMI_SYNC_GEN_OFFSET 0x20200
#define REG_PATTERN_MODE (0 * 4)
#define REG_GLOBAL_CTRL (1 * 4)
#define REG_FRAME_PTR (6 * 4)

// --- Hardware Access Macros ---
// Use these to bypass compiler optimization and ensure memory-mapped I/O is not
// cached
#define IOWR_32DIRECT(base, offset, data)                                      \
  (*(volatile uint32_t *)((uint8_t *)(base) + (offset)) = (data))
#define IORD_32DIRECT(base, offset)                                            \
  (*(volatile uint32_t *)((uint8_t *)(base) + (offset)))

#define IOWR(base, reg, data)                                                  \
  (*(volatile uint32_t *)((uint8_t *)(base) + ((reg) * 4)) = (data))
#define IORD(base, reg)                                                        \
  (*(volatile uint32_t *)((uint8_t *)(base) + ((reg) * 4)))

// MSGDMA CSR Register Offsets
#define MSGDMA_CSR_STATUS 0x00
#define MSGDMA_CSR_CONTROL 0x04
#define MSGDMA_CSR_FILL_LEVEL 0x08

// MSGDMA CSR Status Bits (Official Altera Macros)
#define ALTERA_MSGDMA_CSR_BUSY_MASK 1
#define ALTERA_MSGDMA_CSR_DESCRIPTOR_BUFFER_EMPTY_MASK (1 << 1)
#define ALTERA_MSGDMA_CSR_DESCRIPTOR_BUFFER_FULL_MASK (1 << 2)
#define ALTERA_MSGDMA_CSR_RESPONSE_BUFFER_EMPTY_MASK (1 << 3)
#define ALTERA_MSGDMA_CSR_RESPONSE_BUFFER_FULL_MASK (1 << 4)
#define ALTERA_MSGDMA_CSR_STOP_STATE_MASK (1 << 5)
#define ALTERA_MSGDMA_CSR_RESET_STATE_MASK (1 << 6)

// MSGDMA Descriptor Field Offsets (Standard)
#define MSGDMA_DESC_READ_ADDR 0x00
#define MSGDMA_DESC_WRITE_ADDR 0x04
#define MSGDMA_DESC_LENGTH 0x08
#define MSGDMA_DESC_CONTROL 0x0C

// MSGDMA Descriptor Control Bits
#define MSGDMA_CTRL_GENERATE_SOP (1 << 8)
#define MSGDMA_CTRL_GENERATE_EOP (1 << 9)
#define MSGDMA_CTRL_END_ON_EOP (1 << 12)
#define MSGDMA_CTRL_GO (1U << 31)

// --- Application Context ---
struct app_context {
  int fd_mem;
  void *virtual_ddr_base;
  void *virtual_lwh2f_base;
  volatile uint32_t *read_dma_csr;
  volatile uint32_t *read_dma_desc;
  volatile uint32_t *write_dma_csr;
  volatile uint32_t *write_dma_desc;
  volatile uint32_t *hdmi_csr;
  int raw_sock;
};

// --- Function Prototypes ---
void init_memory_mapping(struct app_context *ctx);
void cleanup_system(struct app_context *ctx);
void enable_hdmi_display(struct app_context *ctx);

// MSGDMA Helpers
void msgdma_init(volatile uint32_t *csr_base);
void msgdma_read_stream_push(volatile uint32_t *descriptor_base,
                             uint32_t src_addr, uint32_t length);
void msgdma_read_stream_push_chunked(volatile uint32_t *descriptor_base,
                                     uint32_t src_addr, uint32_t length,
                                     int is_sop, int is_eop);
void msgdma_write_stream_push(volatile uint32_t *descriptor_base,
                              uint32_t dst_addr, uint32_t length);

void msgdma_reset_pipeline(volatile uint32_t *write_csr,
                           volatile uint32_t *read_csr);

int msgdma_transmit_burst_pipeline(
    volatile uint32_t *write_csr, volatile uint32_t *write_desc,
    volatile uint32_t *read_csr, volatile uint32_t *read_desc,
    uint32_t src_base_addr, uint32_t dst_base_addr, int packet_count,
    uint32_t src_offset, uint32_t write_bytes, uint32_t read_bytes);

#endif // ST2110_COMMON_H
