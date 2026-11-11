#include "i2c.h"

#include "board.h"
#include "clock.h"
#include "gpio.h"

/* TIMINGR for 100 kHz standard mode from a 16 MHz kernel clock (PCLK1): PRESC = 3 (4 MHz, 250 ns),
 * SCLDEL = 4 (1.25 us), SDADEL = 2 (0.5 us), SCLH = 15 (4.0 us), SCLL = 19 (5.0 us).
 * This is the value of the table "Examples of timings settings for fI2CCLK = 16 MHz" in RM0440,
 * written from memory and re-derived from the fields above: check it on a scope at first use. */
#define I2C_TIMING_100K_16MHZ 0x30420F13u

#define I2C_TIMEOUT_MS 5u /* 9 bits at 100 kHz take 90 us */

static bool ok;

#if defined(BOARD_NUCLEO)
/* a bare Nucleo has no pull-ups on D7 / D8; the internal 40k is enough to scan a short Qwiic cable */
#define I2C_PULL GPIO_PULL_UP
#else
#define I2C_PULL GPIO_PULL_NONE /* 2.2k to 3V3 on the board */
#endif

void i2c_lines(bool *sda_high, bool *scl_high)
{
    enum gpio_mode sda_mode = gpio_get_mode(PIN_I2C_SDA);
    enum gpio_mode scl_mode = gpio_get_mode(PIN_I2C_SCL);
    gpio_input(PIN_I2C_SDA, I2C_PULL);
    gpio_input(PIN_I2C_SCL, I2C_PULL);
    delay_ms(1);
    *sda_high = gpio_read(PIN_I2C_SDA);
    *scl_high = gpio_read(PIN_I2C_SCL);
    gpio_set_mode(PIN_I2C_SDA, sda_mode);
    gpio_set_mode(PIN_I2C_SCL, scl_mode);
}

static void peripheral_restart(void)
{
    /* PE low for three APB cycles resets the state machine and releases both lines */
    I2C2->CR1 &= ~I2C_CR1_PE;
    (void)I2C2->CR1;
    (void)I2C2->CR1;
    (void)I2C2->CR1;
    I2C2->CR1 |= I2C_CR1_PE;
}

bool i2c_init(void)
{
    ok = false;
    bool sda, scl;
    gpio_analog(PIN_I2C_SDA);
    gpio_analog(PIN_I2C_SCL);
    i2c_lines(&sda, &scl);
    if (!sda || !scl) {
        gpio_analog(PIN_I2C_SDA);
        gpio_analog(PIN_I2C_SCL);
        return false;
    }

    RCC->APB1ENR1 |= RCC_APB1ENR1_I2C2EN;
    (void)RCC->APB1ENR1;
    I2C2->CR1 = 0;
    I2C2->TIMINGR = I2C_TIMING_100K_16MHZ;
    I2C2->CR1 = I2C_CR1_PE;
    /* open drain: the peripheral only ever pulls low */
    gpio_alternate(PIN_I2C_SDA, AF_I2C_SDA, I2C_PULL, true);
    gpio_alternate(PIN_I2C_SCL, AF_I2C_SCL, I2C_PULL, true);
    ok = true;
    return true;
}

bool i2c_ok(void)
{
    return ok;
}

/* waits for any of `mask` in ISR; false on a timeout (and the peripheral is restarted) */
static bool wait_isr(uint32_t mask)
{
    uint32_t start = millis();
    while (!(I2C2->ISR & mask)) {
        if ((uint32_t)(millis() - start) > I2C_TIMEOUT_MS) {
            peripheral_restart();
            return false;
        }
    }
    return true;
}

static int finish(void)
{
    /* with AUTOEND the peripheral sends the stop itself, after the last byte or after a NACK */
    if (!wait_isr(I2C_ISR_STOPF)) {
        return -1;
    }
    uint32_t isr = I2C2->ISR;
    I2C2->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF;
    if (isr & (I2C_ISR_BERR | I2C_ISR_ARLO)) {
        return -1;
    }
    return (isr & I2C_ISR_NACKF) ? 0 : 1;
}

int i2c_probe(uint8_t addr)
{
    if (!ok) {
        return -1;
    }
    /* address + write, no data bytes, stop */
    I2C2->CR2 = ((uint32_t)addr << 1) | I2C_CR2_AUTOEND | I2C_CR2_START;
    return finish();
}

int i2c_write(uint8_t addr, const uint8_t *data, unsigned len)
{
    if (!ok || len == 0 || len > 255u) {
        return -1;
    }
    I2C2->CR2 = ((uint32_t)addr << 1) | (len << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND | I2C_CR2_START;
    for (unsigned i = 0; i < len; i++) {
        if (!wait_isr(I2C_ISR_TXIS | I2C_ISR_NACKF | I2C_ISR_STOPF)) {
            return -1;
        }
        if (!(I2C2->ISR & I2C_ISR_TXIS)) {
            break; /* not acknowledged: the stop is on its way */
        }
        I2C2->TXDR = data[i];
    }
    return finish();
}

int i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *value)
{
    if (!ok) {
        return -1;
    }
    /* write the register number without a stop ... */
    I2C2->CR2 = ((uint32_t)addr << 1) | (1u << I2C_CR2_NBYTES_Pos) | I2C_CR2_START;
    if (!wait_isr(I2C_ISR_TXIS | I2C_ISR_NACKF)) {
        return -1;
    }
    if (I2C2->ISR & I2C_ISR_NACKF) {
        /* a master that is not acknowledged sends the stop by itself, AUTOEND or not */
        return finish() < 0 ? -1 : 0;
    }
    I2C2->TXDR = reg;
    if (!wait_isr(I2C_ISR_TC | I2C_ISR_NACKF)) {
        return -1;
    }
    if (I2C2->ISR & I2C_ISR_NACKF) {
        return finish() < 0 ? -1 : 0;
    }
    /* ... then a repeated start and one byte back */
    I2C2->CR2 = ((uint32_t)addr << 1) | I2C_CR2_RD_WRN | (1u << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND |
                I2C_CR2_START;
    if (!wait_isr(I2C_ISR_RXNE | I2C_ISR_NACKF | I2C_ISR_STOPF)) {
        return -1;
    }
    bool got = I2C2->ISR & I2C_ISR_RXNE;
    if (got) {
        *value = (uint8_t)I2C2->RXDR;
    }
    int result = finish();
    return result < 0 ? -1 : (got ? 1 : 0);
}

void i2c_wake_token(void)
{
    if (!ok) {
        return;
    }
    /* REVIEW round 5: the ATECC608B wakes on SDA low for 60 us or more. The general-call address is
     * eight zero bits: SDA low for 80 us at 100 kHz. Nobody acknowledges it, which is fine. Then the
     * chip needs 1.5 ms before it listens. (Both times from the data sheet, from memory.) */
    (void)i2c_probe(0x00);
    delay_ms(2);
}
