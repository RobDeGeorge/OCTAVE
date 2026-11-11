/* The console's logic without its UART: line editing, splitting a line into words, finding the command,
 * reading numbers. Pure C, tested on the host (tests/test_console.c). */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONSOLE_LINE_MAX 96 /* including the terminating NUL */
#define CONSOLE_ARGV_MAX 12

struct console {
    char line[CONSOLE_LINE_MAX];
    uint8_t len;
    bool too_long;  /* the line overflowed: it is thrown away at its end, never run cut short */
    bool after_cr;  /* the last character was CR, so a following LF is part of the same line end */
};

enum console_event {
    CONSOLE_NONE,     /* nothing to show */
    CONSOLE_ECHO,     /* the character was added: echo it */
    CONSOLE_ERASE,    /* the last character was removed: send "\b \b" */
    CONSOLE_BELL,     /* the character was refused (line full) */
    CONSOLE_LINE,     /* a line is ready in `line` (may be empty) */
    CONSOLE_TOO_LONG, /* a line ended but had overflowed: nothing to run */
    CONSOLE_CANCEL,   /* Ctrl-C: the line was dropped */
};

void console_reset(struct console *con);

/* Feed one received character. After CONSOLE_LINE the text is in con->line until the next call. */
enum console_event console_feed(struct console *con, char c);

/* Splits `line` in place at spaces and tabs. Returns the number of words, or -1 when there are more
 * than `max`. */
int console_split(char *line, char *argv[], int max);

struct console_cmd {
    const char *name;
    int (*run)(int argc, char *argv[]); /* 0 = ok, CONSOLE_USAGE = print the usage line */
    const char *usage;
    const char *help;
};

#define CONSOLE_USAGE (-2)

/* The command whose name is exactly `name`, or NULL. Names are case sensitive. */
const struct console_cmd *console_find(const struct console_cmd *table, size_t count, const char *name);

/* Decimal, or hexadecimal with 0x. The whole string must be a number that fits in 32 bits. */
bool console_parse_u32(const char *s, uint32_t *out);

/* "on" / "1" -> 1, "off" / "0" -> 0, anything else -> -1. */
int console_parse_onoff(const char *s);

/* Words of two hex digits each ("c1 33 f1 81") into bytes. Returns the count, or -1 on a bad word or
 * more than `max`. */
int console_parse_hex_bytes(int argc, char *argv[], uint8_t *out, int max);

#endif /* CONSOLE_H */
