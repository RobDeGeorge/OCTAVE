#include "console.h"
#include "test.h"

static struct console con;

/* feed a string, return the event of its last character */
static enum console_event feed(const char *s)
{
    enum console_event ev = CONSOLE_NONE;
    for (; *s != '\0'; s++) {
        ev = console_feed(&con, *s);
    }
    return ev;
}

static int calls;
static int last_argc;

static int cmd_count(int argc, char *argv[])
{
    (void)argv;
    calls++;
    last_argc = argc;
    return 0;
}

static void test_line_editing(void)
{
    console_reset(&con);
    CHECK_EQ(console_feed(&con, 'v'), CONSOLE_ECHO);
    CHECK_EQ(feed("bat"), CONSOLE_ECHO);
    CHECK_EQ(console_feed(&con, '\r'), CONSOLE_LINE);
    CHECK_STR(con.line, "vbat");
    /* the LF of a CR LF pair is not a second (empty) line */
    CHECK_EQ(console_feed(&con, '\n'), CONSOLE_NONE);
    /* a lone LF ends a line too */
    CHECK_EQ(feed("help\n"), CONSOLE_LINE);
    CHECK_STR(con.line, "help");
    /* LF LF is two lines, the second empty */
    CHECK_EQ(console_feed(&con, '\n'), CONSOLE_LINE);
    CHECK_STR(con.line, "");

    /* backspace and DEL */
    CHECK_EQ(console_feed(&con, '\b'), CONSOLE_NONE); /* nothing to erase */
    feed("dlc onn");
    CHECK_EQ(console_feed(&con, 0x7f), CONSOLE_ERASE);
    CHECK_EQ(console_feed(&con, '\r'), CONSOLE_LINE);
    CHECK_STR(con.line, "dlc on");

    /* control characters and bytes above ASCII are dropped */
    CHECK_EQ(console_feed(&con, 0x1b), CONSOLE_NONE);
    CHECK_EQ(console_feed(&con, (char)0xff), CONSOLE_NONE);
    CHECK_EQ(feed("ok\r"), CONSOLE_LINE);
    CHECK_STR(con.line, "ok");

    /* Ctrl-C drops the line */
    feed("dlc on");
    CHECK_EQ(console_feed(&con, 0x03), CONSOLE_CANCEL);
    CHECK_EQ(console_feed(&con, '\r'), CONSOLE_LINE);
    CHECK_STR(con.line, "");
}

static void test_overflow(void)
{
    console_reset(&con);
    for (int i = 0; i < CONSOLE_LINE_MAX - 1; i++) {
        CHECK_EQ(console_feed(&con, 'a'), CONSOLE_ECHO);
    }
    /* exactly full is still a valid line */
    CHECK_EQ(console_feed(&con, '\r'), CONSOLE_LINE);
    CHECK_EQ(strlen(con.line), CONSOLE_LINE_MAX - 1);

    /* one more than fits: refused, and the line is thrown away rather than run cut short */
    for (int i = 0; i < CONSOLE_LINE_MAX - 1; i++) {
        console_feed(&con, 'a');
    }
    CHECK_EQ(console_feed(&con, 'b'), CONSOLE_BELL);
    CHECK_EQ(console_feed(&con, '\r'), CONSOLE_TOO_LONG);
    CHECK_STR(con.line, "");
    /* and the next line is normal again */
    CHECK_EQ(feed("vbat\r"), CONSOLE_LINE);
    CHECK_STR(con.line, "vbat");
}

static void test_split(void)
{
    char *argv[4];
    char a[] = "can listen hs 500000";
    CHECK_EQ(console_split(a, argv, 4), 4);
    CHECK_STR(argv[0], "can");
    CHECK_STR(argv[1], "listen");
    CHECK_STR(argv[2], "hs");
    CHECK_STR(argv[3], "500000");

    char b[] = "  \tdlc \t on  ";
    CHECK_EQ(console_split(b, argv, 4), 2);
    CHECK_STR(argv[0], "dlc");
    CHECK_STR(argv[1], "on");

    char c[] = "";
    CHECK_EQ(console_split(c, argv, 4), 0);
    char d[] = "   ";
    CHECK_EQ(console_split(d, argv, 4), 0);
    char e[] = "a b c d e";
    CHECK_EQ(console_split(e, argv, 4), -1);
}

static void test_find(void)
{
    static const struct console_cmd table[] = {
        { "help", cmd_count, "help", "" },
        { "dlc", cmd_count, "dlc on|off", "" },
    };
    char *argv[] = { "dlc", "on" };

    CHECK(console_find(table, 2, "help") == &table[0]);
    CHECK(console_find(table, 2, "dlc") == &table[1]);
    CHECK(console_find(table, 2, "dl") == NULL);   /* no prefix matching: "dl" must not switch anything */
    CHECK(console_find(table, 2, "dlcx") == NULL);
    CHECK(console_find(table, 2, "DLC") == NULL);
    CHECK(console_find(table, 2, "") == NULL);
    CHECK(console_find(table, 0, "help") == NULL);
    CHECK_EQ(console_find(table, 2, "dlc")->run(2, argv), 0);
    CHECK_EQ(calls, 1);
    CHECK_EQ(last_argc, 2);
}

static void test_numbers(void)
{
    uint32_t v = 77;

    CHECK(console_parse_u32("0", &v) && v == 0);
    CHECK(console_parse_u32("500000", &v) && v == 500000);
    CHECK(console_parse_u32("4294967295", &v) && v == 4294967295u);
    CHECK(console_parse_u32("0x7E8", &v) && v == 0x7e8);
    CHECK(console_parse_u32("0XffFFffFF", &v) && v == 0xffffffffu);
    v = 77;
    CHECK(!console_parse_u32("4294967296", &v)); /* overflow */
    CHECK(!console_parse_u32("0x100000000", &v));
    CHECK(!console_parse_u32("", &v));
    CHECK(!console_parse_u32("0x", &v));
    CHECK(!console_parse_u32("12a", &v));
    CHECK(!console_parse_u32("-1", &v));
    CHECK(!console_parse_u32("5 ", &v));
    CHECK_EQ(v, 77); /* a refused string leaves the output alone */

    CHECK_EQ(console_parse_onoff("on"), 1);
    CHECK_EQ(console_parse_onoff("1"), 1);
    CHECK_EQ(console_parse_onoff("off"), 0);
    CHECK_EQ(console_parse_onoff("0"), 0);
    CHECK_EQ(console_parse_onoff("ON"), -1);
    CHECK_EQ(console_parse_onoff("o"), -1);
    CHECK_EQ(console_parse_onoff(""), -1);

    uint8_t bytes[4] = { 0 };
    char *good[] = { "c1", "33", "F1", "81" };
    CHECK_EQ(console_parse_hex_bytes(4, good, bytes, 4), 4);
    CHECK(bytes[0] == 0xc1 && bytes[1] == 0x33 && bytes[2] == 0xf1 && bytes[3] == 0x81);
    CHECK_EQ(console_parse_hex_bytes(0, good, bytes, 4), 0);
    CHECK_EQ(console_parse_hex_bytes(4, good, bytes, 3), -1);
    char *one_digit[] = { "c" };
    char *three_digits[] = { "c13" };
    char *not_hex[] = { "g1" };
    char *prefixed[] = { "0x" };
    CHECK_EQ(console_parse_hex_bytes(1, one_digit, bytes, 4), -1);
    CHECK_EQ(console_parse_hex_bytes(1, three_digits, bytes, 4), -1);
    CHECK_EQ(console_parse_hex_bytes(1, not_hex, bytes, 4), -1);
    CHECK_EQ(console_parse_hex_bytes(1, prefixed, bytes, 4), -1);
}

int main(void)
{
    test_line_editing();
    test_overflow();
    test_split();
    test_find();
    test_numbers();
    return TEST_RESULT();
}
