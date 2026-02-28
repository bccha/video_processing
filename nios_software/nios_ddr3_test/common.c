#include "common.h"

char get_char_polled() {
  unsigned int data;
  while (1) {
    data = IORD_ALTERA_AVALON_JTAG_UART_DATA(JTAG_UART_BASE);
    if (data & ALTERA_AVALON_JTAG_UART_DATA_RVALID_MSK) {
      return (char)(data & ALTERA_AVALON_JTAG_UART_DATA_DATA_MSK);
    }
  }
}

char get_char_async() {
  unsigned int data = IORD_ALTERA_AVALON_JTAG_UART_DATA(JTAG_UART_BASE);
  if (data & ALTERA_AVALON_JTAG_UART_DATA_RVALID_MSK) {
    return (char)(data & ALTERA_AVALON_JTAG_UART_DATA_DATA_MSK);
  }
  return 0;
}

unsigned long long get_total_cycles() {
  unsigned int t1, t2, snap;
  do {
    t1 = alt_nticks();
    IOWR_ALTERA_AVALON_TIMER_SNAPL(TIMER_0_BASE, 0);
    unsigned int low = IORD_ALTERA_AVALON_TIMER_SNAPL(TIMER_0_BASE);
    unsigned int high = IORD_ALTERA_AVALON_TIMER_SNAPH(TIMER_0_BASE);
    snap = (high << 16) | low;
    t2 = alt_nticks();
  } while (t1 != t2);

  unsigned long long cycles = (unsigned long long)t1 * 50000;
  cycles += (49999 - snap);
  return cycles;
}
