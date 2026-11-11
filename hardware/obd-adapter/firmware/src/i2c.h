/* I2C2 on the Qwiic bus: PA8 = SDA, PA9 = SCL, 100 kHz. On board: LIS2DH12 (0x19), ATECC608B (0x60). */
#ifndef I2C_H
#define I2C_H

#include <stdbool.h>
#include <stdint.h>

#define I2C_ADDR_ACCEL  0x19u
#define I2C_ADDR_SECURE 0x60u

/* false: a line is stuck low or has no pull-up (i2c_lines() says which) */
bool i2c_init(void);
bool i2c_ok(void);
void i2c_lines(bool *sda_high, bool *scl_high); /* the two lines read as plain inputs, bus released */

/* 1 = acknowledged, 0 = no answer, -1 = bus error or timeout */
int i2c_probe(uint8_t addr);
int i2c_write(uint8_t addr, const uint8_t *data, unsigned len);
int i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *value);

/* ATECC608B: SDA low for longer than its wake time, then its start-up delay */
void i2c_wake_token(void);

#endif /* I2C_H */
