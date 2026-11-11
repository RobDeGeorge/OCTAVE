#include "can_timing.h"
#include "test.h"

static unsigned quanta(const struct can_timing *t)
{
    return 1u + t->tseg1 + t->tseg2;
}

int main(void)
{
    struct can_timing t;

    /* the board: 16 MHz kernel clock, 500 kbit/s HS-CAN */
    CHECK(can_timing_calc(16000000, 500000, &t));
    CHECK_EQ(t.brp, 2);
    CHECK_EQ(t.tseg1, 13);
    CHECK_EQ(t.tseg2, 2);
    CHECK_EQ(t.sjw, 2);
    CHECK_EQ(can_timing_sample_point(&t), 875);
    CHECK_EQ(can_timing_nbtp(&t), 0x02010C01);

    /* every rate the console offers divides 16 MHz exactly */
    static const uint32_t rates[] = { 1000000, 500000, 250000, 125000, 100000, 83333, 50000, 33333 };
    for (unsigned i = 0; i < sizeof rates / sizeof rates[0]; i++) {
        CHECK(can_timing_calc(16000000, rates[i], &t));
        uint32_t got = 16000000u / (t.brp * quanta(&t));
        uint32_t diff = got > rates[i] ? got - rates[i] : rates[i] - got;
        CHECK(diff * 1000u <= rates[i]);
        CHECK(quanta(&t) >= 8 && quanta(&t) <= 20);
        CHECK(t.brp >= 1 && t.brp <= 512);
        CHECK(t.tseg2 >= 2 && t.sjw <= t.tseg2 && t.sjw <= t.tseg1);
        CHECK(can_timing_sample_point(&t) >= 750 && can_timing_sample_point(&t) <= 900);
    }
    CHECK(can_timing_calc(16000000, 125000, &t));
    CHECK_EQ(t.brp, 8);
    CHECK_EQ(can_timing_nbtp(&t), 0x02070C01);
    CHECK(can_timing_calc(16000000, 1000000, &t));
    CHECK_EQ(t.brp, 1);

    /* refused: zero, faster than the clock allows, a rate 16 MHz cannot make within 0.1 % */
    CHECK(!can_timing_calc(16000000, 0, &t));
    CHECK(!can_timing_calc(0, 500000, &t));
    CHECK(!can_timing_calc(16000000, 4000000, &t));
    CHECK(!can_timing_calc(16000000, 307200, &t));
    /* a rate so low the divider runs out (16 MHz / (512 * 20) = 1562 bit/s) */
    CHECK(!can_timing_calc(16000000, 1000, &t));
    return TEST_RESULT();
}
