/* The start-up pin table (src/safe_pins.c) against the generated pin map (src/board_pins.h) and the
 * rules of ../verify/REVIEW.md that can be checked without hardware. */
#include "board_pins.h"
#include "safe_pins.h"
#include "test.h"

static int find(pin_t pin)
{
    int found = -1, count = 0;
    for (unsigned i = 0; i < safe_pins_count; i++) {
        if (safe_pins[i].pin == pin) {
            found = (int)i;
            count++;
        }
    }
    return count == 1 ? found : -1;
}

static int state(pin_t pin, enum board_id board)
{
    int i = find(pin);
    return i < 0 ? -1 : safe_pins[i].state[board];
}

static int is_input(int s)
{
    return s == PIN_INPUT || s == PIN_INPUT_PULLUP || s == PIN_INPUT_PULLDOWN || s == PIN_ANALOG;
}

int main(void)
{
    /* every pin of the design is in the table exactly once, under its own name: a net added to the
     * board fails here until someone decides what its pin does at start-up */
    unsigned design_pins = 0;
#define X(name, port, bit)                                       \
    do {                                                         \
        int i = find(GPIO_PIN(port, bit));                       \
        design_pins++;                                           \
        CHECK(i >= 0);                                           \
        if (i >= 0) {                                            \
            CHECK_STR(safe_pins[i].net, #name);                  \
        }                                                        \
    } while (0);
    BOARD_PIN_TABLE(X)
#undef X
    CHECK_EQ(safe_pins_count, design_pins);

    for (int b = 0; b < BOARD_ID_COUNT; b++) {
        enum board_id board = (enum board_id)b;

        /* the legacy-bus supply is switched off before anything else */
        CHECK(safe_pins[0].pin == PIN_DLC_SW_EN);
        CHECK_EQ(safe_pins[0].state[board], PIN_OUT_LOW);

        /* bus drivers idle */
        CHECK_EQ(state(PIN_J1850_TXP, board), PIN_OUT_LOW);
        CHECK_EQ(state(PIN_J1850_TXN, board), PIN_OUT_LOW);
        CHECK_EQ(state(PIN_KLINE_TX, board), PIN_OUT_LOW);
        CHECK_EQ(state(PIN_LLINE_TX, board), PIN_OUT_LOW);

        /* CAN: TXD high and before STB; STB high (standby) */
        CHECK_EQ(state(PIN_CAN1_TXD, board), PIN_OUT_HIGH);
        CHECK_EQ(state(PIN_CAN1_STB, board), PIN_OUT_HIGH);
        CHECK_EQ(state(PIN_CAN2_TXD, board), PIN_OUT_HIGH);
        CHECK_EQ(state(PIN_CAN2_STB, board), PIN_OUT_HIGH);
        CHECK(find(PIN_CAN1_TXD) < find(PIN_CAN1_STB));
        CHECK(find(PIN_CAN2_TXD) < find(PIN_CAN2_STB));

        /* inputs that have no resistor on the board */
        CHECK_EQ(state(PIN_KLINE_RX, board), PIN_INPUT_PULLUP);
        CHECK_EQ(state(PIN_BTN_USER, board), PIN_INPUT_PULLUP);

        /* the debug port is never reconfigured */
        CHECK_EQ(state(PIN_SWDIO, board), PIN_SYSTEM);
        CHECK_EQ(state(PIN_SWCLK, board), PIN_SYSTEM);
    }

    /* Rev 0: nothing drives an ESP32 pin; PB6 lets CONFIG switch 1 decide */
    CHECK(is_input(state(PIN_ESP_RTS, BOARD_ID_REV0)));
    CHECK(is_input(state(PIN_ESP_CTS_M, BOARD_ID_REV0)));
    CHECK(is_input(state(PIN_MCU_TX_ESP_M, BOARD_ID_REV0)));
    CHECK(is_input(state(PIN_ESP_TX_MCU, BOARD_ID_REV0)));
    CHECK(is_input(state(PIN_ESP_IO0_M, BOARD_ID_REV0)));
    CHECK(is_input(state(PIN_ESP_EN_M, BOARD_ID_REV0)));
    CHECK_EQ(state(PIN_ESP_PWR_EN, BOARD_ID_REV0), PIN_INPUT_PULLDOWN);

    /* Nucleo: its 32 kHz crystal is left alone, LD2 starts off */
    CHECK_EQ(state(PIN_ACC_INT1, BOARD_ID_NUCLEO), PIN_SYSTEM);
    CHECK_EQ(state(PIN_ACC_INT2, BOARD_ID_NUCLEO), PIN_SYSTEM);
    CHECK_EQ(state(PIN_EXP_PA5, BOARD_ID_NUCLEO), PIN_OUT_LOW);

    /* one EXTI line per pin number: the three interrupt sources of Rev 0 are on different lines
     * (REVIEW round 5). board.c repeats this as a compile-time check on the table it really uses. */
    CHECK(PIN_BIT(PIN_PANIC_BTN) != PIN_BIT(PIN_ACC_INT1));
    CHECK(PIN_BIT(PIN_PANIC_BTN) != PIN_BIT(PIN_BTN_USER));
    CHECK(PIN_BIT(PIN_ACC_INT1) != PIN_BIT(PIN_BTN_USER));

    CHECK_STR(pin_state_name(PIN_OUT_HIGH), "out high");
    return TEST_RESULT();
}
