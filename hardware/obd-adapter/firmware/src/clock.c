#include "clock.h"

#include "board.h"
#include "stm32g4xx.h"

#define HSE_START_TIMEOUT_MS 100u /* a 16 MHz crystal starts in a few ms */
#define SWITCH_TIMEOUT_MS    10u

static volatile uint32_t ticks;
static volatile enum clock_source source = CLOCK_HSI;

void SysTick_Handler(void);
void NMI_Handler(void);

void SysTick_Handler(void)
{
    ticks++;
}

/* The clock security system raises NMI when the crystal stops. The hardware has already switched
 * SYSCLK to HSI16 and turned HSE off; all that is left is to clear the flag and remember it. */
void NMI_Handler(void)
{
    if (RCC->CIFR & RCC_CIFR_CSSF) {
        RCC->CICR = RCC_CICR_CSSC;
        source = CLOCK_HSI_CRYSTAL_FAILED;
    }
}

uint32_t millis(void)
{
    return ticks;
}

void delay_ms(uint32_t ms)
{
    uint32_t start = ticks;
    while ((uint32_t)(ticks - start) <= ms) {
        watchdog_feed();
    }
}

#if defined(BOARD_REV0)
_Static_assert(BOARD_HSE_HZ == CLOCK_HZ, "the crystal and HSI16 must be the same frequency: no PLL here");

static void try_crystal(void)
{
    RCC->CR |= RCC_CR_HSEON;
    uint32_t start = ticks;
    while (!(RCC->CR & RCC_CR_HSERDY) && (uint32_t)(ticks - start) < HSE_START_TIMEOUT_MS) {
    }
    if (!(RCC->CR & RCC_CR_HSERDY)) {
        RCC->CR &= ~RCC_CR_HSEON;
        source = CLOCK_HSI_NO_CRYSTAL;
        return;
    }
    /* same frequency as HSI16, so flash wait states (0 up to 20 MHz), the tick and every baud rate stay */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSE;
    start = ticks;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSE && (uint32_t)(ticks - start) < SWITCH_TIMEOUT_MS) {
    }
    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSE) {
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
        RCC->CR &= ~RCC_CR_HSEON;
        source = CLOCK_HSI_NO_CRYSTAL;
        return;
    }
    source = CLOCK_HSE;
    /* from here a crystal that stops means NMI and HSI16, not a dead chip. HSI16 is left running for that. */
    RCC->CR |= RCC_CR_CSSON;
}
#endif

void clock_init(void)
{
    /* out of reset the G4 runs on HSI16 with no prescalers: start the tick on that */
    SysTick->LOAD = CLOCK_HZ / 1000u - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;

#if defined(BOARD_REV0)
    try_crystal();
#endif

    /* FDCAN kernel clock: PCLK1. The reset choice is HSE, which is off on the Nucleo and after a
     * crystal failure. */
    RCC->CCIPR = (RCC->CCIPR & ~RCC_CCIPR_FDCANSEL) | RCC_CCIPR_FDCANSEL_1;
}

enum clock_source clock_source(void)
{
    return source;
}

const char *clock_source_name(enum clock_source s)
{
    switch (s) {
    case CLOCK_HSI:                return "HSI16 (internal RC)";
    case CLOCK_HSE:                return "HSE (16 MHz crystal)";
    case CLOCK_HSI_NO_CRYSTAL:     return "HSI16: THE CRYSTAL DID NOT START";
    case CLOCK_HSI_CRYSTAL_FAILED: return "HSI16: THE CRYSTAL STOPPED WHILE RUNNING";
    }
    return "?";
}
