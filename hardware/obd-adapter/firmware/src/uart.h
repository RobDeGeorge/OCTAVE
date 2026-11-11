/* The console's serial port, 115200 8N1.
 *   Rev 0:   USART1 on J5 (EXP): pin 5 = TX (PC4), pin 6 = RX (PC5), pins 3 / 4 / 14 = GND
 *   Nucleo:  USART2 on PA2 / PA3 = the ST-LINK's virtual COM port */
#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(void);
void uart_putc(char c);        /* blocking; '\n' is sent as CR LF */
void uart_puts(const char *s);
void uart_flush(void);         /* waits until the last character has left the pin */
int uart_getc(void);           /* the next received character, or -1 */
uint32_t uart_overruns(void);  /* received characters lost so far */

void kprintf(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif /* UART_H */
