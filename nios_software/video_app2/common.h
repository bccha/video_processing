#ifndef COMMON_H_
#define COMMON_H_

#include "altera_avalon_jtag_uart_regs.h"
#include "altera_avalon_timer_regs.h"
#include "io.h"
#include "sys/alt_alarm.h"
#include "sys/alt_cache.h"
#include "system.h"
#include <stdio.h>


// ============================================================================
// Global Configuration
// ============================================================================

// Nios II Data Cache Bypass Mask (Bit 31)
#define CACHE_BYPASS_MASK 0x80000000

// DDR3 Window Base Address
#define DDR3_WINDOW_BASE                                                       \
  (ADDRESS_SPAN_EXTENDER_0_WINDOWED_SLAVE_BASE | CACHE_BYPASS_MASK)

// ============================================================================
// Helper Functions
// ============================================================================

// Blocking version: Waits until a character is received
char get_char_polled();

// Non-blocking (Async) version: Returns char if available, else returns 0
char get_char_async();

// Returns current physical cycles (50MHz) since boot
unsigned long long get_total_cycles();

#endif /* COMMON_H_ */
