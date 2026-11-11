/* What ST's startup file calls before main: SystemInit() straight after reset, before .data and .bss
 * are set up (so it touches registers only), and _init() from __libc_init_array(). */
#include "stm32g4xx.h"

#include "clock.h"

uint32_t SystemCoreClock = CLOCK_HZ;

void _init(void);

void SystemInit(void)
{
    SCB->VTOR = FLASH_BASE;

    /* REVIEW (../verify/REVIEW.md, firmware obligations): "Set PWR_CR3.UCPD_DBDIS early". Until this
     * bit is set the USB-PD dead-battery logic can put a 5.1k pull-down on PB4 (CAN2 TXD) and PB6
     * (ESP32 power enable). These are the first instructions after reset that can do it. */
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC->APB1ENR1; /* a read back: the clock is on before the next access */
    PWR->CR3 |= PWR_CR3_UCPD_DBDIS;
}

/* clock_init() never changes the frequency (16 MHz from HSI16 or from the 16 MHz crystal) */
void SystemCoreClockUpdate(void)
{
    SystemCoreClock = CLOCK_HZ;
}

/* crti.o is not linked (-nostartfiles); newlib's __libc_init_array() still calls this */
void _init(void)
{
}
