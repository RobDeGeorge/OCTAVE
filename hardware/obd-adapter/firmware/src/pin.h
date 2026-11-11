/* A GPIO pin as one byte: port in the high nibble, bit in the low. No hardware access here, so the
 * generated pin map (board_pins.h) can be used by host tests too. */
#ifndef PIN_H
#define PIN_H

#include <stdint.h>

typedef uint8_t pin_t;

enum { PORT_A, PORT_B, PORT_C, PORT_D, PORT_E, PORT_F, PORT_G };

#define GPIO_PIN(port, bit) ((pin_t)((PORT_##port << 4) | (bit)))
#define PIN_PORT(pin)       ((unsigned)(pin) >> 4)
#define PIN_BIT(pin)        ((unsigned)(pin) & 15u)

#endif /* PIN_H */
