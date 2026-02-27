#ifndef ST2110_COMMON_H
#define ST2110_COMMON_H

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

// --- Memory Map Constants (MUST Match Qsys / system.h) ---
#define FPGA_SHARED_DDR_PHYS_ADDR                                              \
  0x30000000 // Base physical address for Ring/Tx Buffer
#define RING_BUFFER_SIZE_BYTES (1920 * 1080 * 4) // E.g., Size for a few frames

// FPGA LWH2F bridge address for DMA Control
#define LWH2F_BASE 0xFF200000
#define LWH2F_SPAN 0x00200000

// MSGDMA RX Read (Memory -> Streaming)
#define RX_DMA_READ_CSR_OFFSET 0x2060
#define RX_DMA_READ_DESCRIPTOR_SLAVE_OFFSET 0x20b0

// MSGDMA RX Write (Streaming -> Memory)
#define RX_DMA_WRITE_CSR_OFFSET 0x2040
#define RX_DMA_WRITE_DESCRIPTOR_SLAVE_OFFSET 0x20a0

// Display Frame Buffer (RTL Output Destination)
#define FPGA_FRAME_BUFFER_PHYS_ADDR 0x20000000

// HDMI Sync Gen CSR Offset
#define HDMI_SYNC_GEN_OFFSET 0x2080
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

// MSGDMA CSR & Descriptor Field Offsets
#define MSGDMA_CSR_STATUS 0x00
#define MSGDMA_CSR_CONTROL 0x04
#define MSGDMA_DESC_READ_ADDR 0x00
#define MSGDMA_DESC_WRITE_ADDR 0x04
#define MSGDMA_DESC_LENGTH 0x08
#define MSGDMA_DESC_CONTROL 0x0C

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

#endif // ST2110_COMMON_H
