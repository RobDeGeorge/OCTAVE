#include "can_timing.h"

bool can_timing_calc(uint32_t clk_hz, uint32_t bitrate, struct can_timing *out)
{
    /* time quanta per bit, in order of preference */
    static const uint8_t quanta[] = { 16, 20, 18, 14, 12, 10, 8 };

    if (bitrate == 0 || clk_hz == 0 || bitrate > clk_hz / 8) {
        return false;
    }
    uint32_t per_bit = (clk_hz + bitrate / 2) / bitrate; /* kernel clock periods in one bit */
    uint64_t actual = (uint64_t)per_bit * bitrate;       /* equals clk_hz when the rate is exact */
    uint64_t error = actual > clk_hz ? actual - clk_hz : clk_hz - actual;
    if (error * 1000 > clk_hz) {
        return false; /* more than 0.1 % off */
    }
    for (unsigned i = 0; i < sizeof quanta; i++) {
        unsigned tq = quanta[i];
        if (per_bit % tq != 0 || per_bit / tq > 512) {
            continue;
        }
        unsigned tseg2 = (tq + 4) / 8;
        if (tseg2 < 2) {
            tseg2 = 2;
        }
        out->brp = (uint16_t)(per_bit / tq);
        out->tseg2 = (uint16_t)tseg2;
        out->tseg1 = (uint16_t)(tq - 1 - tseg2);
        out->sjw = (uint16_t)tseg2; /* as wide as allowed: this node only listens and may run from the RC */
        return true;
    }
    return false;
}

uint32_t can_timing_nbtp(const struct can_timing *t)
{
    /* RM0440 FDCAN_NBTP: NSJW[31:25] NBRP[24:16] NTSEG1[15:8] NTSEG2[6:0] */
    return ((uint32_t)(t->sjw - 1u) << 25) | ((uint32_t)(t->brp - 1u) << 16) | ((uint32_t)(t->tseg1 - 1u) << 8)
           | (uint32_t)(t->tseg2 - 1u);
}

unsigned can_timing_sample_point(const struct can_timing *t)
{
    unsigned total = 1u + t->tseg1 + t->tseg2;
    return (1000u * (1u + t->tseg1) + total / 2) / total;
}
