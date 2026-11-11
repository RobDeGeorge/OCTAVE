#include <limits.h>

#include "fmt.h"
#include "test.h"

int main(void)
{
    char buf[64];

    CHECK_EQ(fmt_snprintf(buf, sizeof buf, "plain"), 5);
    CHECK_STR(buf, "plain");
    fmt_snprintf(buf, sizeof buf, "%d %d %d", 0, 42, -42);
    CHECK_STR(buf, "0 42 -42");
    fmt_snprintf(buf, sizeof buf, "%u", 4294967295u);
    CHECK_STR(buf, "4294967295");
    fmt_snprintf(buf, sizeof buf, "%d", INT_MIN);
    CHECK_STR(buf, "-2147483648");
    fmt_snprintf(buf, sizeof buf, "%ld %lu", -5L, 7UL);
    CHECK_STR(buf, "-5 7");
    fmt_snprintf(buf, sizeof buf, "%x %X %08x %lx", 0xbeefu, 0xbeefu, 0x1234u, 0xcafeUL);
    CHECK_STR(buf, "beef BEEF 00001234 cafe");
    fmt_snprintf(buf, sizeof buf, "[%5d] [%-5d] [%05d] [%5d]", 42, 42, 42, -42);
    CHECK_STR(buf, "[   42] [42   ] [00042] [  -42]");
    fmt_snprintf(buf, sizeof buf, "[%05d]", -42);
    CHECK_STR(buf, "[-0042]");
    fmt_snprintf(buf, sizeof buf, "%u.%02u V", 12u, 6u); /* how millivolts are printed */
    CHECK_STR(buf, "12.06 V");
    fmt_snprintf(buf, sizeof buf, "[%s] [%6s] [%-6s] [%c]", "abc", "abc", "abc", 'x');
    CHECK_STR(buf, "[abc] [   abc] [abc   ] [x]");
    const char *volatile no_text = NULL; /* volatile: the compiler must not see the null at the call */
    fmt_snprintf(buf, sizeof buf, "%s", no_text);
    CHECK_STR(buf, "(null)");
    fmt_snprintf(buf, sizeof buf, "100%%");
    CHECK_STR(buf, "100%");
    /* a width wider than the number of digits a long can have */
    fmt_snprintf(buf, sizeof buf, "%30d|", 1);
    CHECK_EQ(strlen(buf), 31);

    /* truncation: always terminated, returns the full length */
    char small[4];
    CHECK_EQ(fmt_snprintf(small, sizeof small, "%s", "abcdef"), 6);
    CHECK_STR(small, "abc");
    char guard[2] = { 'G', 'G' };
    CHECK_EQ(fmt_snprintf(guard, 0, "abc"), 3);
    CHECK_EQ(guard[0], 'G');
    return TEST_RESULT();
}
