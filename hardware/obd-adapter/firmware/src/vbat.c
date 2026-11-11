#include "vbat.h"

uint32_t vbat_raw_to_mv(uint32_t raw, uint32_t full_scale, uint32_t vref_mv, uint32_t top_ohm, uint32_t bottom_ohm)
{
    if (full_scale == 0 || bottom_ohm == 0) {
        return 0;
    }
    if (raw > full_scale) {
        raw = full_scale;
    }
    /* 4095 * 3300 * 110000 does not fit in 32 bits */
    uint64_t num = (uint64_t)raw * vref_mv * ((uint64_t)top_ohm + bottom_ohm);
    uint64_t den = (uint64_t)full_scale * bottom_ohm;
    return (uint32_t)((num + den / 2) / den);
}

enum vbat_state vbat_classify(uint32_t mv)
{
    if (mv < 3000) {
        return VBAT_NONE;
    }
    if (mv < 9000) {
        return VBAT_LOW;
    }
    if (mv < 16000) {
        return VBAT_12V;
    }
    if (mv < 32000) {
        return VBAT_HIGH;
    }
    return VBAT_OVER;
}

const char *vbat_state_name(enum vbat_state state)
{
    switch (state) {
    case VBAT_NONE:
        return "none (no supply on J1962 pin 16)";
    case VBAT_LOW:
        return "low";
    case VBAT_12V:
        return "12 V system";
    case VBAT_HIGH:
        return "high (24 V system or jump start)";
    case VBAT_OVER:
        return "over range";
    }
    return "?";
}
