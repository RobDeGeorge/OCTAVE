/* GPIO access by pin_t. Every "make it an output" helper writes the level before the mode, so a pin
 * never drives the wrong level for even one instruction. */
#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>

#include "pin.h"
#include "stm32g4xx.h"

enum gpio_mode { GPIO_MODE_INPUT = 0, GPIO_MODE_OUTPUT = 1, GPIO_MODE_AF = 2, GPIO_MODE_ANALOG = 3 };
enum gpio_pull { GPIO_PULL_NONE = 0, GPIO_PULL_UP = 1, GPIO_PULL_DOWN = 2 };

static inline GPIO_TypeDef *gpio_port(pin_t pin)
{
    /* GPIOA..GPIOG are 0x400 apart (RM0440 memory map) */
    return (GPIO_TypeDef *)(GPIOA_BASE + 0x400u * PIN_PORT(pin));
}

static inline void gpio_write(pin_t pin, bool level)
{
    /* BSRR: one write, no read-modify-write, safe from any context */
    gpio_port(pin)->BSRR = level ? (1u << PIN_BIT(pin)) : (1u << (PIN_BIT(pin) + 16u));
}

static inline bool gpio_read(pin_t pin)
{
    return (gpio_port(pin)->IDR >> PIN_BIT(pin)) & 1u;
}

/* the level the output latch holds, whatever the mode */
static inline bool gpio_latch(pin_t pin)
{
    return (gpio_port(pin)->ODR >> PIN_BIT(pin)) & 1u;
}

static inline enum gpio_mode gpio_get_mode(pin_t pin)
{
    return (enum gpio_mode)((gpio_port(pin)->MODER >> (2u * PIN_BIT(pin))) & 3u);
}

static inline void gpio_set_mode(pin_t pin, enum gpio_mode mode)
{
    GPIO_TypeDef *port = gpio_port(pin);
    unsigned shift = 2u * PIN_BIT(pin);
    port->MODER = (port->MODER & ~(3u << shift)) | ((uint32_t)mode << shift);
}

static inline void gpio_set_pull(pin_t pin, enum gpio_pull pull)
{
    GPIO_TypeDef *port = gpio_port(pin);
    unsigned shift = 2u * PIN_BIT(pin);
    port->PUPDR = (port->PUPDR & ~(3u << shift)) | ((uint32_t)pull << shift);
}

static inline void gpio_set_open_drain(pin_t pin, bool open_drain)
{
    GPIO_TypeDef *port = gpio_port(pin);
    if (open_drain) {
        port->OTYPER |= 1u << PIN_BIT(pin);
    } else {
        port->OTYPER &= ~(1u << PIN_BIT(pin));
    }
}

static inline void gpio_set_af(pin_t pin, unsigned af)
{
    GPIO_TypeDef *port = gpio_port(pin);
    unsigned shift = 4u * (PIN_BIT(pin) & 7u);
    volatile uint32_t *afr = &port->AFR[PIN_BIT(pin) >> 3];
    *afr = (*afr & ~(15u << shift)) | ((uint32_t)af << shift);
}

/* push-pull output at `level`: latch first, mode last */
static inline void gpio_output(pin_t pin, bool level)
{
    gpio_write(pin, level);
    gpio_set_open_drain(pin, false);
    gpio_set_pull(pin, GPIO_PULL_NONE);
    gpio_set_mode(pin, GPIO_MODE_OUTPUT);
}

/* open-drain output: `released` true = high impedance, false = pulling low */
static inline void gpio_output_open_drain(pin_t pin, bool released)
{
    gpio_write(pin, released);
    gpio_set_open_drain(pin, true);
    gpio_set_mode(pin, GPIO_MODE_OUTPUT);
}

/* digital input: pull first, mode last */
static inline void gpio_input(pin_t pin, enum gpio_pull pull)
{
    gpio_set_pull(pin, pull);
    gpio_set_mode(pin, GPIO_MODE_INPUT);
}

/* high impedance, input buffer off (the reset state of most pins) */
static inline void gpio_analog(pin_t pin)
{
    gpio_set_mode(pin, GPIO_MODE_ANALOG);
    gpio_set_pull(pin, GPIO_PULL_NONE);
}

/* hand the pin to a peripheral: function number and pull first, mode last */
static inline void gpio_alternate(pin_t pin, unsigned af, enum gpio_pull pull, bool open_drain)
{
    gpio_set_af(pin, af);
    gpio_set_pull(pin, pull);
    gpio_set_open_drain(pin, open_drain);
    gpio_set_mode(pin, GPIO_MODE_AF);
}

#endif /* GPIO_H */
