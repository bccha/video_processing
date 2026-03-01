#ifndef HDMI_CONFIG_H_
#define HDMI_CONFIG_H_

#include <stdint.h>

// ADV7513 7-bit Slave Address (0x72 >> 1)
#define ADV7513_ADDR 0x39

// Core Initialization Function
int hdmi_init();

// Generic I2C Write helper
void hdmi_i2c_write(uint8_t reg, uint8_t data);

#endif /* HDMI_CONFIG_H_ */
