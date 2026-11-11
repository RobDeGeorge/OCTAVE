#include "fmt.h"

#include <stdbool.h>

struct out {
    fmt_put_fn put;
    void *ctx;
    int count;
};

static void emit(struct out *o, char c)
{
    o->put(c, o->ctx);
    o->count++;
}

static void pad(struct out *o, int n, char c)
{
    while (n-- > 0) {
        emit(o, c);
    }
}

/* digits of `value` in `base`, least significant first; returns how many */
static int digits(unsigned long value, unsigned base, bool upper, char *tmp)
{
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    do {
        tmp[n++] = set[value % base];
        value /= base;
    } while (value != 0);
    return n;
}

static void number(struct out *o, unsigned long value, bool negative, unsigned base, bool upper, int width,
                   bool zero, bool left)
{
    char tmp[3 * sizeof(unsigned long)];
    int n = digits(value, base, upper, tmp);
    int fill = width - n - (negative ? 1 : 0);

    if (!left && !zero) {
        pad(o, fill, ' ');
    }
    if (negative) {
        emit(o, '-');
    }
    if (!left && zero) {
        pad(o, fill, '0');
    }
    while (n > 0) {
        emit(o, tmp[--n]);
    }
    if (left) {
        pad(o, fill, ' ');
    }
}

int fmt_vformat(fmt_put_fn put, void *ctx, const char *format, va_list ap)
{
    struct out o = { put, ctx, 0 };

    for (const char *p = format; *p != '\0'; p++) {
        if (*p != '%') {
            emit(&o, *p);
            continue;
        }
        p++;
        bool zero = false, left = false, is_long = false;
        int width = 0;
        for (; *p == '0' || *p == '-'; p++) {
            if (*p == '0') {
                zero = true;
            } else {
                left = true;
            }
        }
        for (; *p >= '0' && *p <= '9'; p++) {
            width = width * 10 + (*p - '0');
        }
        if (*p == 'l') {
            is_long = true;
            p++;
        }
        switch (*p) {
        case 'd':
        case 'i': {
            long v = is_long ? va_arg(ap, long) : va_arg(ap, int);
            /* negate as unsigned so the most negative value does not overflow */
            unsigned long mag = v < 0 ? 0ul - (unsigned long)v : (unsigned long)v;
            number(&o, mag, v < 0, 10, false, width, zero, left);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            unsigned long v = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            number(&o, v, false, *p == 'u' ? 10 : 16, *p == 'X', width, zero, left);
            break;
        }
        case 'c':
            if (!left) {
                pad(&o, width - 1, ' ');
            }
            emit(&o, (char)va_arg(ap, int));
            if (left) {
                pad(&o, width - 1, ' ');
            }
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            int n = 0;
            if (s == NULL) {
                s = "(null)";
            }
            while (s[n] != '\0') {
                n++;
            }
            if (!left) {
                pad(&o, width - n, ' ');
            }
            for (int i = 0; i < n; i++) {
                emit(&o, s[i]);
            }
            if (left) {
                pad(&o, width - n, ' ');
            }
            break;
        }
        case '%':
            emit(&o, '%');
            break;
        case '\0':
            /* a lone % at the end: print it and stop */
            emit(&o, '%');
            return o.count;
        default:
            /* unknown conversion: show it as written, so the mistake is visible on the console */
            emit(&o, '%');
            emit(&o, *p);
            break;
        }
    }
    return o.count;
}

struct buffer {
    char *buf;
    size_t size;
    size_t used;
};

static void buffer_put(char c, void *ctx)
{
    struct buffer *b = ctx;
    if (b->used + 1 < b->size) {
        b->buf[b->used] = c;
    }
    b->used++;
}

int fmt_snprintf(char *buf, size_t size, const char *format, ...)
{
    struct buffer b = { buf, size, 0 };
    va_list ap;

    va_start(ap, format);
    int n = fmt_vformat(buffer_put, &b, format, ap);
    va_end(ap);
    if (size > 0) {
        buf[b.used < size ? b.used : size - 1] = '\0';
    }
    return n;
}
