/* The state every pin is put in at start-up, before anything else runs: one table for both boards,
 * applied top to bottom by board_init_safe(). Data only, so the host test (tests/test_pins.c) can check
 * it against the generated pin map: every pin listed exactly once, the order rules, the ESP32 rules. */
#ifndef SAFE_PINS_H
#define SAFE_PINS_H

#include <stdint.h>

#include "pin.h"

enum board_id { BOARD_ID_REV0, BOARD_ID_NUCLEO, BOARD_ID_COUNT };

enum pin_state {
    PIN_SYSTEM,         /* never touched: SWD, the oscillator, NRST */
    PIN_ANALOG,         /* high impedance, input buffer off (reads 0) */
    PIN_INPUT,          /* high impedance, readable */
    PIN_INPUT_PULLUP,
    PIN_INPUT_PULLDOWN,
    PIN_OUT_LOW,        /* push-pull */
    PIN_OUT_HIGH,       /* push-pull */
};

struct safe_pin {
    pin_t pin;
    const char *net;
    uint8_t state[BOARD_ID_COUNT]; /* enum pin_state per board */
};

extern const struct safe_pin safe_pins[];
extern const unsigned safe_pins_count;

const char *pin_state_name(enum pin_state state);

#endif /* SAFE_PINS_H */
