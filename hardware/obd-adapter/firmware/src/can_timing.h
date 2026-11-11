/* Classic CAN bit timing for the FDCAN nominal bit timing register. Pure C, tested on the host
 * (tests/test_can_timing.c). */
#ifndef CAN_TIMING_H
#define CAN_TIMING_H

#include <stdbool.h>
#include <stdint.h>

struct can_timing {
    uint16_t brp;   /* clock divider, 1..512 */
    uint16_t tseg1; /* time quanta before the sample point, without the sync segment (prop + phase 1) */
    uint16_t tseg2; /* time quanta after the sample point */
    uint16_t sjw;   /* resynchronisation jump width in time quanta */
};

/* Finds a timing for `bitrate` from a kernel clock of `clk_hz`: 8 to 20 time quanta per bit, sample point
 * near 87.5 %, the bit rate within 0.1 % of the request. False when there is none. */
bool can_timing_calc(uint32_t clk_hz, uint32_t bitrate, struct can_timing *out);

/* The value of FDCAN_NBTP for a timing (each field holds its value minus one). */
uint32_t can_timing_nbtp(const struct can_timing *t);

/* Sample point in tenths of a percent (875 = 87.5 %). */
unsigned can_timing_sample_point(const struct can_timing *t);

#endif /* CAN_TIMING_H */
