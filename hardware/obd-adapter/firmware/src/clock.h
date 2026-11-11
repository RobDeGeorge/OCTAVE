/* System clock (16 MHz, no PLL), the 1 ms tick and delays. */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

#define CLOCK_HZ 16000000u /* SYSCLK = HCLK = PCLK1 = PCLK2, from either source */

enum clock_source {
    CLOCK_HSI,                /* internal 16 MHz RC, by design (Nucleo) */
    CLOCK_HSE,                /* the board's 16 MHz crystal */
    CLOCK_HSI_NO_CRYSTAL,     /* the crystal did not start: running on the internal RC instead */
    CLOCK_HSI_CRYSTAL_FAILED, /* the crystal stopped while running: the clock security system switched over */
};

/* Starts the tick on HSI16, then (Rev 0) tries the crystal. Never hangs: without a working crystal
 * the system stays on HSI16 and clock_source() says so. */
void clock_init(void);
enum clock_source clock_source(void);
const char *clock_source_name(enum clock_source source);

uint32_t millis(void);
/* Waits at least `ms` milliseconds, feeding the watchdog. Not for interrupt handlers. */
void delay_ms(uint32_t ms);

#endif /* CLOCK_H */
