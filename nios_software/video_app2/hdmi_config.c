#include "hdmi_config.h"
#include "altera_avalon_i2c.h"
#include "altera_avalon_pio_regs.h"
#include "system.h"
#include <stdio.h>
#include <unistd.h>

static ALT_AVALON_I2C_DEV_t *i2c_dev;

void hdmi_i2c_write(uint8_t reg, uint8_t data) {
  uint8_t buffer[2];
  buffer[0] = reg;
  buffer[1] = data;

  // Set target slave address
  alt_avalon_i2c_master_target_set(i2c_dev, ADV7513_ADDR);

  // Transmit 2 bytes (Reg addr + Data)
  // use_interrupts = 0 (No interrupts)
  alt_avalon_i2c_master_tx(i2c_dev, buffer, 2, 0);
}

int hdmi_init() {
  int timeout = 1000; // 1 second timeout (1ms * 1000)
  printf("Waiting for PLL Lock (74.25 MHz)...\n");

  // Wait until PLL is locked with timeout
  while (!(IORD_ALTERA_AVALON_PIO_DATA(PLL_LOCKED_BASE))) {
    usleep(1000); // Wait 1ms
    if (--timeout == 0) {
      printf("Error: PLL Lock Timeout! Check Clock settings.\n");
      return -1; // Fail
    }
  }
  printf("PLL Locked! Initializing ADV7513 HDMI Transmitter...\n");

  // Get I2C Device Handle using HAL name
  i2c_dev = alt_avalon_i2c_open(I2C_HDMI_NAME);
  if (!i2c_dev) {
    printf("Error: Could not open I2C device %s\n", I2C_HDMI_NAME);
    return -2; // Fail
  }

  // Set Speed to 100kHz (Optional but explicit)
  ALT_AVALON_I2C_MASTER_CONFIG_t cfg;
  alt_avalon_i2c_master_config_get(i2c_dev, &cfg);
  alt_avalon_i2c_master_config_speed_set(i2c_dev, &cfg, 100000);
  alt_avalon_i2c_master_config_set(i2c_dev, &cfg);

  // --- ADV7513 Initialization Sequence ---
  hdmi_i2c_write(0x41, 0x10);
  hdmi_i2c_write(0x16, 0x00);
  hdmi_i2c_write(0xAF, 0x06);
  hdmi_i2c_write(0x3C, 0x18);

  hdmi_i2c_write(0x98, 0x03);
  hdmi_i2c_write(0x9A, 0xE0);
  hdmi_i2c_write(0x9C, 0x30);
  hdmi_i2c_write(0x9D, 0x61);
  hdmi_i2c_write(0xA2, 0xA4);
  hdmi_i2c_write(0xA3, 0xA4);
  hdmi_i2c_write(0xE0, 0xD0);
  hdmi_i2c_write(0xF9, 0x00);

  printf("HDMI Controller Configured. Ready for Video!\n");
  return 0; // Success
}
