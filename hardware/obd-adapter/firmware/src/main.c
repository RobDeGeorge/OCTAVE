/* OCTAVE OBD adapter: bring-up firmware. See ../README.md. */
#include <stdint.h>

#include "adc.h"
#include "board.h"
#include "clock.h"
#include "cmds.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "uart.h"

/* ------------------------------------------------------------------ faults
 *
 * Any fault: bus drivers off first, then say where it happened, then wait for the watchdog. The next
 * boot reports "reset by: WATCHDOG". */

void HardFault_Handler(void);
void hardfault_c(const uint32_t *frame);

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile("tst lr, #4\n"
                   "ite eq\n"
                   "mrseq r0, msp\n"
                   "mrsne r0, psp\n"
                   "b hardfault_c\n");
}

__attribute__((used)) void hardfault_c(const uint32_t *frame)
{
    board_force_safe();
    led_set(true, false, false);
    /* the stacked frame: r0 r1 r2 r3 r12 lr pc xpsr */
    kprintf("\n*** FAULT pc=%08x lr=%08x cfsr=%08x hfsr=%08x addr=%08x\n", (unsigned)frame[6],
            (unsigned)frame[5], (unsigned)SCB->CFSR, (unsigned)SCB->HFSR, (unsigned)SCB->BFAR);
    kprintf("*** bus drivers off; waiting for the watchdog\n");
    for (;;) {
    }
}

/* ------------------------------------------------------------------ start-up */

static void report_pin(const struct safe_pin *pin, bool driven, bool read)
{
    kprintf("PIN FAULT: %s (P%c%u) is driven %s but reads %s\n", pin->net, "ABCDEFG"[PIN_PORT(pin->pin)],
            PIN_BIT(pin->pin), driven ? "high" : "low", read ? "high" : "low");
}

static void banner(void)
{
    kprintf("\n\nOCTAVE OBD adapter bring-up firmware %s, board: %s\n", FW_VERSION, FW_BOARD_NAME);
    kprintf("clock:    %s\n", clock_source_name(clock_source()));
    kprintf("reset by: %s\n", board_reset_cause());
    if (board_boot0_switch()) {
        kprintf("NOTE: BOOT0 is high (CONFIG switch 4): the next reset starts the ROM bootloader\n");
    }
    unsigned wrong = board_check_outputs(report_pin);
    if (wrong) {
        kprintf("%u output pin%s read the wrong level: look for a short before going on\n", wrong,
                wrong == 1 ? "" : "s");
    }
}

int main(void)
{
    /* Before anything else, and before the clock is touched: every pin to its safe state. Until this
     * line the pins are in their reset state (analog, high impedance), which the board's own pull-down
     * resistors turn into "everything off". */
    board_init_safe();
    clock_init();
    watchdog_start();
    uart_init();

    /* lamp test: red, green, blue */
    led_set(true, false, false);
    delay_ms(150);
    led_set(false, true, false);
    delay_ms(150);
    led_set(false, false, true);
    delay_ms(150);
    led_set(false, false, false);

    banner();
    if (!adc_init()) {
        kprintf("ADC: did not start (vbat will not work)\n");
    }
    if (!i2c_init()) {
        kprintf("I2C: a line reads low: no pull-ups or a stuck device (try `i2c scan` for details)\n");
    }
    board_events_init();
    kprintf("type `help`\n> ");

    static struct console con;
    console_reset(&con);
    uint32_t seen[8] = { 0 };

    for (;;) {
        watchdog_feed();
        cmds_poll();

        int c = uart_getc();
        if (c >= 0) {
            switch (console_feed(&con, (char)c)) {
            case CONSOLE_ECHO:
                uart_putc((char)c);
                break;
            case CONSOLE_ERASE:
                uart_puts("\b \b");
                break;
            case CONSOLE_BELL:
                uart_putc('\a');
                break;
            case CONSOLE_LINE:
                uart_putc('\n');
                cmds_run(con.line);
                uart_puts("> ");
                break;
            case CONSOLE_TOO_LONG:
                uart_puts("\nline too long, ignored\n> ");
                break;
            case CONSOLE_CANCEL:
                uart_puts("^C\n> ");
                break;
            case CONSOLE_NONE:
                break;
            }
        }

        /* heartbeat: a 50 ms flash every second. Green on the crystal (Rev 0) or the internal
         * oscillator (Nucleo); red when the crystal is missing or has stopped. Blue = button held. */
        bool flash = millis() % 1000u < 50u;
        bool clock_ok = clock_source() == CLOCK_HSE || clock_source() == CLOCK_HSI;
        led_set(flash && !clock_ok, flash && clock_ok, board_button());

        for (unsigned i = 0; i < board_event_sources() && i < 8u; i++) {
            uint32_t count = board_event_count(i);
            if (count != seen[i]) {
                seen[i] = count;
                kprintf("\n[%s: %u]\n> ", board_event_name(i), (unsigned)count);
            }
        }
    }
}
