#include "console.h"

#include <string.h>

void console_reset(struct console *con)
{
    con->len = 0;
    con->line[0] = '\0';
    con->too_long = false;
    con->after_cr = false;
}

static enum console_event end_of_line(struct console *con)
{
    bool too_long = con->too_long;

    con->line[con->len] = '\0';
    con->len = 0;
    con->too_long = false;
    if (too_long) {
        con->line[0] = '\0';
        return CONSOLE_TOO_LONG;
    }
    return CONSOLE_LINE;
}

enum console_event console_feed(struct console *con, char c)
{
    bool after_cr = con->after_cr;

    con->after_cr = (c == '\r');
    if (c == '\r') {
        return end_of_line(con);
    }
    if (c == '\n') {
        return after_cr ? CONSOLE_NONE : end_of_line(con);
    }
    if (c == '\b' || c == 0x7f) {
        if (con->len == 0) {
            return CONSOLE_NONE;
        }
        con->len--;
        return CONSOLE_ERASE;
    }
    if (c == 0x03) {
        con->len = 0;
        con->too_long = false;
        return CONSOLE_CANCEL;
    }
    if (c < 0x20 || c > 0x7e) {
        return CONSOLE_NONE;
    }
    if (con->len >= CONSOLE_LINE_MAX - 1) {
        con->too_long = true;
        return CONSOLE_BELL;
    }
    con->line[con->len++] = c;
    return CONSOLE_ECHO;
}

int console_split(char *line, char *argv[], int max)
{
    int argc = 0;
    char *p = line;

    for (;;) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            return argc;
        }
        if (argc == max) {
            return -1;
        }
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
}

const struct console_cmd *console_find(const struct console_cmd *table, size_t count, const char *name)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(table[i].name, name) == 0) {
            return &table[i];
        }
    }
    return NULL;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool console_parse_u32(const char *s, uint32_t *out)
{
    uint32_t base = 10;
    uint32_t value = 0;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    if (*s == '\0') {
        return false;
    }
    for (; *s != '\0'; s++) {
        int d = hex_digit(*s);
        if (d < 0 || (uint32_t)d >= base) {
            return false;
        }
        if (value > (UINT32_MAX - (uint32_t)d) / base) {
            return false;
        }
        value = value * base + (uint32_t)d;
    }
    *out = value;
    return true;
}

int console_parse_onoff(const char *s)
{
    if (strcmp(s, "on") == 0 || strcmp(s, "1") == 0) {
        return 1;
    }
    if (strcmp(s, "off") == 0 || strcmp(s, "0") == 0) {
        return 0;
    }
    return -1;
}

int console_parse_hex_bytes(int argc, char *argv[], uint8_t *out, int max)
{
    if (argc > max) {
        return -1;
    }
    for (int i = 0; i < argc; i++) {
        int hi = hex_digit(argv[i][0]);
        int lo = hi < 0 ? -1 : hex_digit(argv[i][1]);
        if (lo < 0 || argv[i][2] != '\0') {
            return -1;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return argc;
}
