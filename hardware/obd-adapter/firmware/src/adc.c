#include "adc.h"

#include "board.h"
#include "clock.h"
#include "gpio.h"
#include "vbat.h"

#if VBAT_ADC_UNIT != 2
#error "the board moved VBAT_SENSE to another ADC: this file drives ADC2"
#endif

/* The supply is taken as its nominal value. VREFINT, which would measure it, is not connected to ADC2
 * (it is on ADC1, 3, 4 and 5): reading it means bringing up a second converter. A named next step. */
#define VDDA_MV   3300u
#define ADC_FULL  4095u
#define ADC_VBAT  ADC2

static bool ready;

static bool wait_clear(volatile uint32_t *reg, uint32_t mask, uint32_t timeout_ms)
{
    uint32_t start = millis();
    while (*reg & mask) {
        if ((uint32_t)(millis() - start) > timeout_ms) {
            return false;
        }
    }
    return true;
}

static bool wait_set(volatile uint32_t *reg, uint32_t mask, uint32_t timeout_ms)
{
    uint32_t start = millis();
    while (!(*reg & mask)) {
        if ((uint32_t)(millis() - start) > timeout_ms) {
            return false;
        }
    }
    return true;
}

bool adc_init(void)
{
    ready = false;
    gpio_analog(PIN_VBAT_SENSE);

    RCC->AHB2ENR |= RCC_AHB2ENR_ADC12EN;
    (void)RCC->AHB2ENR;
    /* clock from the bus (HCLK / 4 = 4 MHz), so no PLL and no kernel clock selection is needed */
    ADC12_COMMON->CCR = (ADC12_COMMON->CCR & ~ADC_CCR_CKMODE) | (3u << ADC_CCR_CKMODE_Pos);

    /* ADC_CR's command bits are "write 1 to set": every write below is the whole register, on purpose */
    ADC_VBAT->CR = 0;               /* leave deep power down */
    ADC_VBAT->CR = ADC_CR_ADVREGEN; /* regulator on */
    delay_ms(2);                    /* regulator start-up: 20 us max in the datasheet */

    ADC_VBAT->CR = ADC_CR_ADVREGEN | ADC_CR_ADCAL; /* single-ended calibration */
    if (!wait_clear(&ADC_VBAT->CR, ADC_CR_ADCAL, 50)) {
        return false;
    }
    delay_ms(1); /* ADEN may not be set within 4 ADC clock cycles of ADCAL clearing */

    ADC_VBAT->ISR = ADC_ISR_ADRDY; /* write 1 to clear */
    ADC_VBAT->CR = ADC_CR_ADVREGEN | ADC_CR_ADEN;
    if (!wait_set(&ADC_VBAT->ISR, ADC_ISR_ADRDY, 50)) {
        return false;
    }

    /* The source is 100k || 10k = 9.1k with 100 nF across the pin: the capacitor supplies the sampling
     * charge, the longest sampling time (640.5 cycles = 160 us) costs nothing at this rate. */
#if VBAT_ADC_CHANNEL >= 10
    ADC_VBAT->SMPR2 = 7u << (3u * (VBAT_ADC_CHANNEL - 10u));
#else
    ADC_VBAT->SMPR1 = 7u << (3u * VBAT_ADC_CHANNEL);
#endif
    ADC_VBAT->SQR1 = VBAT_ADC_CHANNEL << ADC_SQR1_SQ1_Pos; /* one conversion, this channel */
    ADC_VBAT->CFGR = (ADC_VBAT->CFGR & ~(ADC_CFGR_RES | ADC_CFGR_CONT | ADC_CFGR_ALIGN));

    ready = true;
    return true;
}

bool adc_read_vbat_mv(uint32_t *mv)
{
    if (!ready) {
        return false;
    }
    uint32_t sum = 0;
    for (unsigned i = 0; i < 16u; i++) {
        ADC_VBAT->CR = ADC_CR_ADVREGEN | ADC_CR_ADSTART;
        if (!wait_set(&ADC_VBAT->ISR, ADC_ISR_EOC, 5)) {
            return false;
        }
        sum += ADC_VBAT->DR & 0xFFFFu; /* reading DR clears EOC */
    }
    *mv = vbat_raw_to_mv((sum + 8u) / 16u, ADC_FULL, VDDA_MV, VBAT_DIV_TOP_OHM, VBAT_DIV_BOTTOM_OHM);
    return true;
}
