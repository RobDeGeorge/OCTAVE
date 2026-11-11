#include "board_pins.h"
#include "test.h"
#include "vbat.h"

/* the board's divider and a 12-bit ADC on a 3.300 V reference */
static uint32_t board_mv(uint32_t raw)
{
    return vbat_raw_to_mv(raw, 4095, 3300, VBAT_DIV_TOP_OHM, VBAT_DIV_BOTTOM_OHM);
}

int main(void)
{
    /* the generated header carries the design's 100k / 10k divider: x11 */
    CHECK_EQ(VBAT_DIV_TOP_OHM, 100000);
    CHECK_EQ(VBAT_DIV_BOTTOM_OHM, 10000);
    CHECK_EQ(VBAT_ADC_CHANNEL, 17);

    CHECK_EQ(board_mv(0), 0);
    CHECK_EQ(board_mv(4095), 36300);          /* full scale: 3.3 V x 11 */
    CHECK_EQ(board_mv(5000), 36300);          /* a code above full scale is clamped */
    /* 12.6 V battery: 1.14545 V at the pin = code 1421.4 */
    CHECK_EQ(board_mv(1421), 12596);
    CHECK_EQ(board_mv(1422), 12605);
    /* 14.4 V charging: code 1624.4 */
    CHECK_EQ(board_mv(1624), 14396);
    /* one code is 8.86 mV at the battery */
    CHECK_EQ(board_mv(1), 9);
    CHECK_EQ(board_mv(2), 18);

    /* rounding to nearest, not down: 1 code of a 1:1 divider on 3300 mV is 1.61 mV */
    CHECK_EQ(vbat_raw_to_mv(1, 4095, 3300, 1000, 1000), 2);
    /* no divider */
    CHECK_EQ(vbat_raw_to_mv(4095, 4095, 3300, 0, 10000), 3300);
    /* a 5 V reference and a bigger divider must not overflow 32 bits on the way */
    CHECK_EQ(vbat_raw_to_mv(65535, 65535, 5000, 1000000, 10000), 505000);
    /* nonsense in, zero out, no division by zero */
    CHECK_EQ(vbat_raw_to_mv(100, 0, 3300, 100000, 10000), 0);
    CHECK_EQ(vbat_raw_to_mv(100, 4095, 3300, 100000, 0), 0);

    CHECK_EQ(vbat_classify(0), VBAT_NONE);
    CHECK_EQ(vbat_classify(2999), VBAT_NONE);
    CHECK_EQ(vbat_classify(3000), VBAT_LOW);
    CHECK_EQ(vbat_classify(8999), VBAT_LOW);
    CHECK_EQ(vbat_classify(9000), VBAT_12V);
    CHECK_EQ(vbat_classify(12600), VBAT_12V);
    CHECK_EQ(vbat_classify(14400), VBAT_12V);
    CHECK_EQ(vbat_classify(16000), VBAT_HIGH);
    CHECK_EQ(vbat_classify(28000), VBAT_HIGH);
    CHECK_EQ(vbat_classify(32000), VBAT_OVER);
    for (int s = VBAT_NONE; s <= VBAT_OVER; s++) {
        CHECK(vbat_state_name((enum vbat_state)s)[0] != '?');
    }
    return TEST_RESULT();
}
