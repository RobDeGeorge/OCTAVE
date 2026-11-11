/* A small printf: %d %i %u %x %X %c %s %%, flags 0 and -, a width, the l length modifier.
 * No floating point and no heap. Pure C, tested on the host (tests/test_fmt.c). */
#ifndef FMT_H
#define FMT_H

#include <stdarg.h>
#include <stddef.h>

typedef void (*fmt_put_fn)(char c, void *ctx);

/* Formats through `put`. Returns the number of characters put. */
int fmt_vformat(fmt_put_fn put, void *ctx, const char *format, va_list ap);

/* Like snprintf: always NUL terminates when size > 0, returns the length the full text needs. */
int fmt_snprintf(char *buf, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));

#endif /* FMT_H */
