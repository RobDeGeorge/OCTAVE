/* Battery voltage from the ADC reading of the divider. Pure C, tested on the host (tests/test_vbat.c). */
#ifndef VBAT_H
#define VBAT_H

#include <stdint.h>

/* Millivolts at the top of a resistor divider.
 *   raw         ADC code, 0..full_scale
 *   full_scale  largest code (4095 for 12 bits)
 *   vref_mv     ADC reference in millivolts
 *   top_ohm     resistor from the measured voltage to the ADC pin
 *   bottom_ohm  resistor from the ADC pin to ground (must not be 0)
 * Rounded to the nearest millivolt. A raw above full_scale is treated as full_scale. */
uint32_t vbat_raw_to_mv(uint32_t raw, uint32_t full_scale, uint32_t vref_mv, uint32_t top_ohm, uint32_t bottom_ohm);

enum vbat_state {
    VBAT_NONE, /* below 3.0 V: nothing on J1962 pin 16 (USB or bench supply only) */
    VBAT_LOW,  /* below 9.0 V: flat battery, cranking dip, or a bench supply turned down */
    VBAT_12V,  /* 9.0 to 16.0 V: a 12 V car, engine off or running */
    VBAT_HIGH, /* 16.0 to 32.0 V: a 24 V vehicle or a jump start */
    VBAT_OVER, /* above 32.0 V: at the edge of what the divider can measure; the TVS is about to clamp */
};

enum vbat_state vbat_classify(uint32_t mv);
const char *vbat_state_name(enum vbat_state state);

#endif /* VBAT_H */
