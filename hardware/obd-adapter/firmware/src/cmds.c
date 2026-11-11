#include "cmds.h"

#include <string.h>

#include "adc.h"
#include "board.h"
#include "can.h"
#include "clock.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "kline.h"
#include "uart.h"
#include "vbat.h"

#define ARM_TIMEOUT_MS 120000u

static bool armed;
static uint32_t armed_at;

static bool key_pressed(void)
{
    return uart_getc() >= 0;
}

static void print_vbat(void)
{
    uint32_t mv;
    if (!adc_read_vbat_mv(&mv)) {
        kprintf("vbat: the ADC did not answer\n");
        return;
    }
    kprintf("vbat: %u.%02u V  (%s)\n", (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 10u),
            vbat_state_name(vbat_classify(mv)));
}

/* ------------------------------------------------------------------ status, pins, vbat */

static int cmd_status(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    uint32_t up = millis();
    kprintf("firmware:  %s for %s\n", FW_VERSION, FW_BOARD_NAME);
    kprintf("up:        %u.%03u s\n", (unsigned)(up / 1000u), (unsigned)(up % 1000u));
    kprintf("clock:     %s\n", clock_source_name(clock_source()));
    kprintf("reset by:  %s\n", board_reset_cause());
    print_vbat();
    kprintf("dlc_sw:    %s\n", board_dlc_is_on() ? "ON" : "off");
    kprintf("can:       %s\n", can_listening() ? "listening" : "standby");
    kprintf("i2c:       %s\n", i2c_ok() ? "ready" : "NOT READY (a line is low)");
#if BOARD_HAS_ESP32
    static const char *const modes[] = { "auto (CONFIG switch 1 decides)", "on", "off" };
    kprintf("esp32:     %s; EN reads %s\n", modes[board_esp_get_mode()],
            board_esp_rail_up() ? "high (powered, running)" : "low (off or held in reset)");
#endif
    kprintf("boot0:     %s\n", board_boot0_switch() ? "HIGH: the next reset starts the ROM bootloader" : "low");
    kprintf("button:    %s\n", board_button() ? "pressed" : "up");
    for (unsigned i = 0; i < board_event_sources(); i++) {
        kprintf("events:    %u x %s\n", (unsigned)board_event_count(i), board_event_name(i));
    }
    kprintf("armed:     %s\n", armed ? "YES (bus transmit allowed)" : "no");
    kprintf("console:   %u characters lost\n", (unsigned)uart_overruns());
    return 0;
}

static int cmd_pins(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    static const char *const modes[] = { "in", "out", "af", "analog" };
    static const char *const pulls[] = { "", " pull-up", " pull-down", "" };
    kprintf("%-15s %-5s %-11s now\n", "net", "pin", "at start");
    for (unsigned i = 0; i < safe_pins_count; i++) {
        const struct safe_pin *p = &safe_pins[i];
        enum pin_state state = (enum pin_state)p->state[THIS_BOARD];
        kprintf("%-15s P%c%-3u %-11s ", p->net, "ABCDEFG"[PIN_PORT(p->pin)], PIN_BIT(p->pin),
                pin_state_name(state));
        if (state == PIN_SYSTEM) {
            kprintf("-\n"); /* SWD, crystal, reset: not touched, not even read */
            continue;
        }
        GPIO_TypeDef *port = gpio_port(p->pin);
        enum gpio_mode mode = gpio_get_mode(p->pin);
        unsigned pull = (port->PUPDR >> (2u * PIN_BIT(p->pin))) & 3u;
        kprintf("%s%s", modes[mode], pulls[pull]);
        if (mode == GPIO_MODE_AF) {
            kprintf(" %u", (unsigned)((port->AFR[PIN_BIT(p->pin) >> 3] >> (4u * (PIN_BIT(p->pin) & 7u))) & 15u));
        }
        if (mode == GPIO_MODE_OUTPUT) {
            bool latch = gpio_latch(p->pin), level = gpio_read(p->pin);
            kprintf(" %s%s", latch ? "high" : "low",
                    latch == level ? "" : "  <-- THE PIN READS THE OTHER LEVEL");
        } else if (mode != GPIO_MODE_ANALOG) {
            kprintf(", reads %s", gpio_read(p->pin) ? "high" : "low");
        }
        kprintf("\n");
        watchdog_feed();
    }
    return 0;
}

static int cmd_vbat(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    print_vbat();
    return 0;
}

/* ------------------------------------------------------------------ dlc, esp */

static int cmd_dlc(int argc, char *argv[])
{
    if (argc == 1) {
        kprintf("dlc_sw is %s\n", board_dlc_is_on() ? "ON" : "off");
        return 0;
    }
    int on = argc == 2 ? console_parse_onoff(argv[1]) : -1;
    if (on < 0) {
        return CONSOLE_USAGE;
    }
    if (!on) {
        armed = false;
        kline_stop();
    }
    board_dlc(on);
    kprintf("dlc_sw %s\n", on ? "ON: battery on the J1850 supply and the K / L pull-ups (TP5)" : "off");
    return 0;
}

static int cmd_esp(int argc, char *argv[])
{
#if BOARD_HAS_ESP32
    if (argc == 2 && !strcmp(argv[1], "auto")) {
        board_esp_mode(ESP_AUTO);
    } else if (argc == 2 && !strcmp(argv[1], "on")) {
        board_esp_mode(ESP_ON);
    } else if (argc == 2 && !strcmp(argv[1], "off")) {
        board_esp_mode(ESP_OFF);
    } else if (argc == 2 && !strcmp(argv[1], "reset")) {
        board_esp_reset();
        kprintf("EN pulled low for 20 ms\n");
    } else if (argc != 1) {
        return CONSOLE_USAGE;
    }
    delay_ms(20); /* the load switch and the 10k / 1 uF on EN */
    static const char *const modes[] = { "auto", "on", "off" };
    bool up = board_esp_rail_up();
    kprintf("esp32: %s; EN reads %s\n", modes[board_esp_get_mode()], up ? "high" : "low");
    if (board_esp_get_mode() == ESP_OFF && up) {
        kprintf("still powered: a cable in J7 keeps the ESP32 on whatever PB6 does\n");
    }
    return 0;
#else
    (void)argc;
    (void)argv;
    kprintf("no ESP32 on this board\n");
    return 0;
#endif
}

/* ------------------------------------------------------------------ i2c */

static int cmd_i2c(int argc, char *argv[])
{
    if (argc != 2 || strcmp(argv[1], "scan")) {
        return CONSOLE_USAGE;
    }
    if (!i2c_ok() && !i2c_init()) {
        bool sda, scl;
        i2c_lines(&sda, &scl);
        kprintf("i2c: SDA reads %s, SCL reads %s: no pull-ups, or something holds the bus\n",
                sda ? "high" : "LOW", scl ? "high" : "LOW");
        return 0;
    }
    i2c_wake_token(); /* the ATECC608B sleeps and would not answer */
    bool accel = false, secure = false;
    unsigned found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        int r = i2c_probe(addr);
        if (r < 0) {
            kprintf("i2c: bus error at 0x%02x, scan stopped\n", addr);
            return 0;
        }
        if (r == 1) {
            found++;
            accel |= addr == I2C_ADDR_ACCEL;
            secure |= addr == I2C_ADDR_SECURE;
            kprintf("  0x%02x%s\n", addr,
                    addr == I2C_ADDR_ACCEL    ? "  LIS2DH12 accelerometer"
                    : addr == I2C_ADDR_SECURE ? "  ATECC608B secure element"
                                              : "");
        }
    }
    kprintf("%u device%s\n", found, found == 1 ? "" : "s");
    if (accel) {
        uint8_t who = 0;
        int r = i2c_read_reg(I2C_ADDR_ACCEL, 0x0F, &who); /* WHO_AM_I */
        kprintf("0x19 WHO_AM_I = 0x%02x (%s)\n", who, r == 1 && who == 0x33 ? "LIS2DH12" : "NOT 0x33");
    }
    if (secure) {
        /* REVIEW round 5: "send it back to sleep". Word address 0x01 = sleep (from memory). */
        static const uint8_t sleep_cmd[] = { 0x01 };
        (void)i2c_write(I2C_ADDR_SECURE, sleep_cmd, sizeof sleep_cmd);
    }
#if defined(BOARD_REV0)
    if (!accel || !secure) {
        kprintf("MISSING:%s%s\n", accel ? "" : " 0x19 (accelerometer)", secure ? "" : " 0x60 (secure element)");
    }
#endif
    return 0;
}

/* ------------------------------------------------------------------ can */

static int cmd_can(int argc, char *argv[])
{
    uint32_t bus = 1, bitrate = 500000;
    if (argc < 2 || argc > 4 || strcmp(argv[1], "listen")) {
        return CONSOLE_USAGE;
    }
    if (argc >= 3 && (!console_parse_u32(argv[2], &bus) || bus < 1u || bus > 2u)) {
        return CONSOLE_USAGE;
    }
    if (argc >= 4 && !console_parse_u32(argv[3], &bitrate)) {
        return CONSOLE_USAGE;
    }

    uint32_t mv = 0;
    if (adc_read_vbat_mv(&mv) && vbat_classify(mv) < VBAT_12V) {
        kprintf("note: %u.%u V on the battery pin. The CAN transceivers run from the 5 V buck converter,\n"
                "      which needs the battery: on USB power alone nothing will be received.\n",
                (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 100u));
    }
    if (clock_source() != CLOCK_HSE) {
        kprintf("note: running on the internal oscillator (1 %% accuracy): marginal for CAN.\n");
    }

    enum can_start result = can_listen_start(bus, bitrate);
    if (result != CAN_OK) {
        kprintf("can: %s\n", can_start_name(result));
        return 0;
    }
    kprintf("CAN%u (J1962 pins %s) at %u bit/s, monitoring only: no transmit, no acknowledge.\n"
            "Any key stops.\n",
            (unsigned)bus, bus == 1u ? "6 / 14" : "3 / 11", (unsigned)bitrate);

    uint32_t start = millis(), blue_until = 0;
    while (!key_pressed()) {
        watchdog_feed();
        struct can_frame f;
        if (can_get(&f)) {
            uint32_t t = f.ms - start;
            kprintf("%5u.%03u  ", (unsigned)(t / 1000u), (unsigned)(t % 1000u));
            if (f.extended) {
                kprintf("%08X", (unsigned)f.id);
            } else {
                kprintf("     %03X", (unsigned)f.id);
            }
            kprintf("  [%u]", f.dlc);
            if (f.remote) {
                kprintf("  remote request");
            } else {
                for (unsigned i = 0; i < f.dlc; i++) {
                    kprintf(" %02X", f.data[i]);
                }
            }
            kprintf("\n");
            blue_until = millis() + 30u;
        }
        led_set(false, false, (int32_t)(blue_until - millis()) > 0);
    }
    struct can_stats s;
    can_get_stats(&s);
    can_listen_stop();
    led_set(false, false, false);
    kprintf("stopped: %u frames received, %u not printed (console too slow), %u lost in the controller\n",
            (unsigned)s.received, (unsigned)s.dropped, (unsigned)s.lost);
    kprintf("         %u bus errors, last: %s, receive error counter %u\n", (unsigned)s.errors,
            can_error_name(s.last_error), s.rx_error_count);
    if (s.received == 0 && s.errors == 0) {
        kprintf("         nothing at all: bus asleep (ignition off?), wrong pins, or no supply\n");
    } else if (s.received == 0) {
        kprintf("         errors only: wrong bit rate, or a bench set-up where nobody acknowledges\n");
    }
    return 0;
}

/* ------------------------------------------------------------------ k-line */

static int cmd_arm(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    armed = true;
    armed_at = millis();
    kprintf("ARMED for two minutes: `kline send` will drive the car's K line.\n"
            "Engine off. `disarm` or `dlc off` ends it.\n");
    return 0;
}

static int cmd_disarm(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    armed = false;
    kprintf("disarmed\n");
    return 0;
}

static int cmd_kline(int argc, char *argv[])
{
    if (argc >= 2 && !strcmp(argv[1], "listen") && argc == 2) {
        if (!board_dlc_is_on()) {
            kprintf("note: dlc_sw is off, so the K line has no pull-up from this board.\n");
        }
        kline_start();
        kprintf("K line at 10400 baud, receive only. The line reads %s. Any key stops.\n",
                kline_line_high() ? "high (idle)" : "LOW");
        unsigned column = 0;
        while (!key_pressed()) {
            watchdog_feed();
            int c = kline_getc();
            if (c >= 0) {
                kprintf("%02X%s", (unsigned)c, ++column % 16u ? " " : "\n");
            }
        }
        kprintf("\nstopped: %u receive errors\n", (unsigned)kline_errors());
        kline_stop();
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "send")) {
        uint8_t bytes[CONSOLE_ARGV_MAX];
        int n = console_parse_hex_bytes(argc - 2, argv + 2, bytes, (int)sizeof bytes);
        if (n <= 0) {
            return CONSOLE_USAGE;
        }
        if (!armed) {
            kprintf("refused: not armed. `arm` first (and read what it says).\n");
            return 0;
        }
        if (!board_dlc_is_on()) {
            kprintf("refused: `dlc on` first, the K line pull-up comes from DLC_SW.\n");
            return 0;
        }
        kline_start();
        delay_ms(5);
        if (!kline_line_high()) {
            kprintf("refused: the K line reads low (no battery, a short, or somebody is talking).\n");
            kline_stop();
            return 0;
        }
        int sent = kline_send(bytes, (unsigned)n);
        if (sent != n) {
            kprintf("STOPPED at byte %d of %d: the echo did not match. Transmitter released.\n", sent + 1, n);
            kline_stop();
            return 0;
        }
        kprintf("sent %d bytes, echo ok. Answer (300 ms):", n);
        uint32_t start = millis();
        while ((uint32_t)(millis() - start) < 300u) {
            watchdog_feed();
            int c = kline_getc();
            if (c >= 0) {
                kprintf(" %02X", (unsigned)c);
            }
        }
        kprintf("\n");
        kline_stop();
        return 0;
    }
    return CONSOLE_USAGE;
}

/* ------------------------------------------------------------------ reset, help */

static int cmd_reset(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    kprintf("resetting\n");
    uart_flush();
    board_force_safe();
    NVIC_SystemReset();
    return 0;
}

static int cmd_help(int argc, char *argv[]);

static const struct console_cmd commands[] = {
    { "help", cmd_help, "help", "this list" },
    { "status", cmd_status, "status", "clock, reset cause, battery, switches, counters" },
    { "pins", cmd_pins, "pins", "every pin: its start-up state and what it is now" },
    { "vbat", cmd_vbat, "vbat", "battery voltage at J1962 pin 16" },
    { "dlc", cmd_dlc, "dlc [on|off]", "battery to the J1850 / K-line side (DLC_SW)" },
    { "esp", cmd_esp, "esp [auto|on|off|reset]", "ESP32 power (PB6) and reset (EN)" },
    { "i2c", cmd_i2c, "i2c scan", "scan the Qwiic bus; expects 0x19 and 0x60" },
    { "can", cmd_can, "can listen [1|2] [bitrate]", "print CAN frames; never transmits or acknowledges" },
    { "kline", cmd_kline, "kline listen | kline send <hex bytes>", "K line at 10400 baud; send needs `arm`" },
    { "arm", cmd_arm, "arm", "allow `kline send` for two minutes" },
    { "disarm", cmd_disarm, "disarm", "forbid it again" },
    { "reset", cmd_reset, "reset", "restart the firmware" },
};

#define COMMAND_COUNT (sizeof commands / sizeof commands[0])

static int cmd_help(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    for (unsigned i = 0; i < COMMAND_COUNT; i++) {
        kprintf("  %-40s %s\n", commands[i].usage, commands[i].help);
    }
    return 0;
}

void cmds_run(char *line)
{
    char *argv[CONSOLE_ARGV_MAX];
    int argc = console_split(line, argv, CONSOLE_ARGV_MAX);
    if (argc == 0) {
        return;
    }
    if (argc < 0) {
        kprintf("too many words\n");
        return;
    }
    const struct console_cmd *cmd = console_find(commands, COMMAND_COUNT, argv[0]);
    if (!cmd) {
        kprintf("unknown command `%s` (try help)\n", argv[0]);
        return;
    }
    if (cmd->run(argc, argv) == CONSOLE_USAGE) {
        kprintf("usage: %s\n", cmd->usage);
    }
}

void cmds_poll(void)
{
    if (armed && (uint32_t)(millis() - armed_at) > ARM_TIMEOUT_MS) {
        armed = false;
        kprintf("\ndisarmed (two minutes are over)\n");
    }
}
