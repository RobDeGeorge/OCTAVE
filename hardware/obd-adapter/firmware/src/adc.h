/* Battery voltage: VBAT_SENSE through the 100k / 10k divider on ADC2. */
#ifndef ADC_H
#define ADC_H

#include <stdbool.h>
#include <stdint.h>

bool adc_init(void);              /* false: the converter did not calibrate or did not become ready */
bool adc_read_vbat_mv(uint32_t *mv); /* average of 16 conversions; false on a timeout */

#endif /* ADC_H */
