/* K-line (ISO 9141-2 / ISO 14230) on LPUART1 at 10400 baud: PC0 receives, PC1 drives the line low
 * through an NPN, so the transmitter is inverted. Listening is always allowed. Sending is not part of
 * the first power-up: the console asks for `arm` first.
 * Not here yet: the 5-baud and fast initialisation sequences, the L line. */
#ifndef KLINE_H
#define KLINE_H

#include <stdbool.h>
#include <stdint.h>

void kline_start(void);  /* receiver on (idempotent) */
void kline_stop(void);   /* receiver off, TX pin back to a low GPIO */
bool kline_line_high(void); /* the K line is at its idle level right now */
int kline_getc(void);    /* next received byte, or -1 */
uint32_t kline_errors(void); /* framing / noise / overrun errors since kline_start() */

/* Sends `len` bytes, checking each against its own echo, 5 ms apart. Returns the number of bytes that
 * went out and came back equal: less than `len` means the line was busy, stuck or shorted, and the
 * transmitter was stopped at that byte. */
int kline_send(const uint8_t *data, unsigned len);

#endif /* KLINE_H */
