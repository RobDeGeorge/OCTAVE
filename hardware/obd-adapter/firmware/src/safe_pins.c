#include "safe_pins.h"

#include "board_pins.h"

#define P(net, rev0, nucleo) { PIN_##net, #net, { rev0, nucleo } }

/* "REVIEW" is ../verify/REVIEW.md, section "Firmware obligations (all rounds)".
 *
 * Hardware already holds every bus driver off while a pin is high impedance (pull-downs on the
 * gates and bases, the transceivers' own STB pull-ups), so reset, a blank chip and the ROM bootloader
 * are safe. This table makes the same state explicit and readable, in an order that never passes
 * through an unsafe one. The Nucleo column drives the same pins the same way wherever the Nucleo has
 * nothing of its own on them, so the levels can be measured on its headers. */
const struct safe_pin safe_pins[] = {
    /* Legacy-bus supply first: DLC_SW off (2N7002 gate, 100k to GND on the board). Off = no J1850
     * supply, no comparator supply, no K / L pull-ups: the board draws nothing from those bus pins. */
    P(DLC_SW_EN, PIN_OUT_LOW, PIN_OUT_LOW),

    /* J1850 drivers idle: TXP low = Bus+ P-FET off, TXN low = Bus- sink off. VSEL low = the 7.24 V
     * VPW supply setting (high shorts the 470R for 4.79 V PWM); it only matters with DLC_SW on. */
    P(J1850_TXP, PIN_OUT_LOW, PIN_OUT_LOW),
    P(J1850_TXN, PIN_OUT_LOW, PIN_OUT_LOW),
    P(J1850_VSEL, PIN_OUT_LOW, PIN_OUT_LOW),

    /* K and L line sinks off: TX high pulls the line low through the NPN, so idle is low.
     * REVIEW: "Set LPUART1 TXINV before switching PC1 to its alternate function": PC1 stays a GPIO
     * at low here; kline.c is the only place that hands it to LPUART1, after TXINV. */
    P(KLINE_TX, PIN_OUT_LOW, PIN_OUT_LOW),
    P(LLINE_TX, PIN_OUT_LOW, PIN_OUT_LOW),

    /* REVIEW: "put PB4/PB13 in AF (TXD high) before driving STB low. Never drive STB low while TXD
     * is low." TXD is driven high as a GPIO first, then STB high (standby). This firmware never
     * gives TXD to the FDCAN at all (it only listens), so TXD is high whenever STB is low. */
    P(CAN1_TXD, PIN_OUT_HIGH, PIN_OUT_HIGH),
    P(CAN1_STB, PIN_OUT_HIGH, PIN_OUT_HIGH),
    P(CAN2_TXD, PIN_OUT_HIGH, PIN_OUT_HIGH),
    P(CAN2_STB, PIN_OUT_HIGH, PIN_OUT_HIGH),
    P(CAN1_RXD, PIN_INPUT, PIN_INPUT),
    P(CAN2_RXD, PIN_INPUT, PIN_INPUT),

    /* REVIEW item 23: "Keep PA1/PA2/PB5/PB7 low or as inputs until the ESP32 is powered, and drive
     * ESP_EN open-drain." All six ESP32 lines are inputs here and nothing in this firmware makes
     * the UART lines outputs. PB7 (EN) is only ever pulled low open-drain (board_esp_reset()).
     * REVIEW: "Leave PB5 (IO0) and PB7 (EN) as inputs while the ESP32 is being flashed through J7."
     * On the Nucleo PA2 / PA3 are the ST-LINK serial port; uart.c takes them after this table. */
    P(ESP_RTS, PIN_INPUT, PIN_INPUT),
    P(ESP_CTS_M, PIN_INPUT, PIN_INPUT),
    P(MCU_TX_ESP_M, PIN_INPUT, PIN_INPUT),
    P(ESP_TX_MCU, PIN_INPUT, PIN_INPUT),
    P(ESP_IO0_M, PIN_INPUT, PIN_INPUT),
    P(ESP_EN_M, PIN_INPUT, PIN_INPUT),
    /* PB6: the ESP32 load switch, OR'd with CONFIG switch 1 (3V3 through 1k) and VBUS2. As an input
     * with the pull-down the switch decides ("esp auto"); PB6 then reads the switch position. */
    P(ESP_PWR_EN, PIN_INPUT_PULLDOWN, PIN_ANALOG),

    /* REVIEW round 6: "Turn on PC0's pull-up before enabling LPUART1's receiver" (KLINE_RX is an
     * open-collector output with no resistor on the board). On from the start. */
    P(KLINE_RX, PIN_INPUT_PULLUP, PIN_INPUT_PULLUP),
    /* J1850 comparator outputs, 10k pull-ups on the board. Inputs until there is a J1850 codec. */
    P(VPW_RX, PIN_INPUT, PIN_INPUT),
    P(PWM_RX, PIN_INPUT, PIN_INPUT),

    /* REVIEW round 6: "keep PA15's pull-up on when the pin becomes a GPIO (it is on from reset, as
     * JTDI; a plain input without PUPDR set would float)". gpio_input() writes the pull before the mode. */
    P(BTN_USER, PIN_INPUT_PULLUP, PIN_INPUT_PULLUP),
    /* PC13: PANIC button, 10k pull-up on the board, closed = low. Nucleo: B1, pressed = high, its own
     * pull-down. No internal pull on either. */
    P(PANIC_BTN, PIN_INPUT, PIN_INPUT),
    /* REVIEW round 5: no 32 kHz crystal, PC14 / PC15 are the accelerometer's interrupt outputs (push-pull,
     * active high). The pull-downs give a level if U15 is not fitted. Nucleo: the LSE crystal, left alone. */
    P(ACC_INT1, PIN_INPUT_PULLDOWN, PIN_SYSTEM),
    P(ACC_INT2, PIN_INPUT_PULLDOWN, PIN_SYSTEM),
    /* PB8 is BOOT0 at reset (10k to GND; CONFIG switch 4 ties it to 3V3). Read as an input, to warn. */
    P(BOOT0, PIN_INPUT, PIN_INPUT),

    /* LEDs: pin -> resistor -> LED -> GND, on = high */
    P(LED_R, PIN_OUT_LOW, PIN_OUT_LOW),
    P(LED_G, PIN_OUT_LOW, PIN_OUT_LOW),
    P(LED_B, PIN_OUT_LOW, PIN_OUT_LOW),

    /* taken later by their drivers: adc.c, i2c.c (4.7k pull-ups on the board), uart.c (rev0 console) */
    P(VBAT_SENSE, PIN_ANALOG, PIN_ANALOG),
    P(I2C_SDA, PIN_ANALOG, PIN_ANALOG),
    P(I2C_SCL, PIN_ANALOG, PIN_ANALOG),
    P(EXP_TX, PIN_ANALOG, PIN_ANALOG),
    P(EXP_RX, PIN_ANALOG, PIN_ANALOG),

    /* QSPI flash: not used yet. NCS has a 10k pull-up on the board, so the flash stays deselected.
     * REVIEW: "Set the W25Q128 QE bit before using quad mode" applies to the driver that is not here. */
    P(QSPI_NCS, PIN_ANALOG, PIN_ANALOG),
    P(QSPI_CLK, PIN_ANALOG, PIN_ANALOG),
    P(QSPI_IO0, PIN_ANALOG, PIN_ANALOG),
    P(QSPI_IO1, PIN_ANALOG, PIN_ANALOG),
    P(QSPI_IO2, PIN_ANALOG, PIN_ANALOG),
    P(QSPI_IO3, PIN_ANALOG, PIN_ANALOG),

    /* USB: the reset state. No USB stack in this firmware; the ROM bootloader (DFU) does its own. */
    P(USB_DM, PIN_ANALOG, PIN_ANALOG),
    P(USB_DP, PIN_ANALOG, PIN_ANALOG),

    /* spare pins on J5. PA5 is LD2 on the Nucleo. */
    P(EXP_PA5, PIN_ANALOG, PIN_OUT_LOW),
    P(EXP_PA10, PIN_ANALOG, PIN_ANALOG),
    P(EXP_PC10, PIN_ANALOG, PIN_ANALOG),
    P(EXP_PD2, PIN_ANALOG, PIN_ANALOG),

    /* debug port, oscillator, reset */
    P(SWDIO, PIN_SYSTEM, PIN_SYSTEM),
    P(SWCLK, PIN_SYSTEM, PIN_SYSTEM),
    P(HSE_IN, PIN_SYSTEM, PIN_SYSTEM),
    P(HSE_OUT, PIN_SYSTEM, PIN_SYSTEM),
    P(NRST, PIN_SYSTEM, PIN_SYSTEM),
};

const unsigned safe_pins_count = sizeof safe_pins / sizeof safe_pins[0];

const char *pin_state_name(enum pin_state state)
{
    switch (state) {
    case PIN_SYSTEM:         return "system";
    case PIN_ANALOG:         return "analog";
    case PIN_INPUT:          return "input";
    case PIN_INPUT_PULLUP:   return "input pull-up";
    case PIN_INPUT_PULLDOWN: return "input pull-down";
    case PIN_OUT_LOW:        return "out low";
    case PIN_OUT_HIGH:       return "out high";
    }
    return "?";
}
