#include "board.h"

#include "clock.h"
#include "gpio.h"

/* "REVIEW" in the comments is ../verify/REVIEW.md, section "Firmware obligations (all rounds)". */

static void apply(pin_t pin, enum pin_state state)
{
    switch (state) {
    case PIN_SYSTEM:
        break;
    case PIN_ANALOG:
        gpio_analog(pin);
        break;
    case PIN_INPUT:
        gpio_input(pin, GPIO_PULL_NONE);
        break;
    case PIN_INPUT_PULLUP:
        gpio_input(pin, GPIO_PULL_UP);
        break;
    case PIN_INPUT_PULLDOWN:
        gpio_input(pin, GPIO_PULL_DOWN);
        break;
    case PIN_OUT_LOW:
        gpio_output(pin, false); /* level first, mode last: see gpio.h */
        break;
    case PIN_OUT_HIGH:
        gpio_output(pin, true);
        break;
    }
}

void board_init_safe(void)
{
    /* PWR_CR3.UCPD_DBDIS is already set: SystemInit() in system.c, before .data is even copied. */
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN | RCC_AHB2ENR_GPIODEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;

    /* the order of the table is the order of the rules: DLC_SW off, bus drivers idle, CAN TXD high
     * before STB, ESP32 pins inputs. Every entry carries its reason in safe_pins.c. */
    for (unsigned i = 0; i < safe_pins_count; i++) {
        apply(safe_pins[i].pin, (enum pin_state)safe_pins[i].state[THIS_BOARD]);
    }
}

void board_force_safe(void)
{
    gpio_output(PIN_DLC_SW_EN, false);
    gpio_output(PIN_J1850_TXP, false);
    gpio_output(PIN_J1850_TXN, false);
    gpio_output(PIN_KLINE_TX, false);
    gpio_output(PIN_LLINE_TX, false);
    /* REVIEW: never STB low while TXD is low. TXD high first, then standby. */
    gpio_output(PIN_CAN1_TXD, true);
    gpio_output(PIN_CAN1_STB, true);
    gpio_output(PIN_CAN2_TXD, true);
    gpio_output(PIN_CAN2_STB, true);
}

unsigned board_check_outputs(void (*report)(const struct safe_pin *pin, bool driven, bool read))
{
    unsigned wrong = 0;
    for (unsigned i = 0; i < safe_pins_count; i++) {
        const struct safe_pin *p = &safe_pins[i];
        if (gpio_get_mode(p->pin) != GPIO_MODE_OUTPUT || p->state[THIS_BOARD] == PIN_SYSTEM) {
            continue;
        }
        if (gpio_port(p->pin)->OTYPER & (1u << PIN_BIT(p->pin))) {
            continue; /* open drain: a low pin with a high latch is somebody else pulling, not a fault */
        }
        bool driven = gpio_latch(p->pin);
        bool read = gpio_read(p->pin);
        if (driven != read) {
            wrong++;
            if (report) {
                report(p, driven, read);
            }
        }
    }
    return wrong;
}

void led_set(bool red, bool green, bool blue)
{
    gpio_write(PIN_LED_R, red);
    gpio_write(PIN_LED_G, green);
    gpio_write(PIN_LED_B, blue);
#if defined(BOARD_NUCLEO)
    gpio_write(PIN_EXP_PA5, red || green || blue); /* LD2: the Nucleo's one LED shows any colour */
#endif
}

bool board_button(void)
{
#if defined(BOARD_NUCLEO)
    return gpio_read(PIN_PANIC_BTN); /* B1 on PC13: pressed = high */
#else
    return !gpio_read(PIN_BTN_USER); /* SW3 on PA15 to GND: pressed = low */
#endif
}

bool board_boot0_switch(void)
{
    return gpio_read(PIN_BOOT0);
}

void board_dlc(bool on)
{
    if (!on) {
        /* the drivers that DLC_SW feeds go idle before their supply does */
        gpio_output(PIN_J1850_TXP, false);
        gpio_output(PIN_J1850_TXN, false);
        gpio_output(PIN_KLINE_TX, false);
        gpio_output(PIN_LLINE_TX, false);
    }
    gpio_write(PIN_DLC_SW_EN, on);
}

bool board_dlc_is_on(void)
{
    return gpio_latch(PIN_DLC_SW_EN);
}

/* ------------------------------------------------------------------ ESP32 power and reset */

static enum esp_mode esp_mode_now = ESP_AUTO;

void board_esp_mode(enum esp_mode mode)
{
#if BOARD_HAS_ESP32
    /* REVIEW item 23: only PB6 changes here. The six signal pins stay inputs whatever the mode, so
     * nothing can back-power the module through a signal pin while its rail is off. */
    switch (mode) {
    case ESP_AUTO:
        gpio_input(PIN_ESP_PWR_EN, GPIO_PULL_DOWN);
        break;
    case ESP_ON:
        gpio_output(PIN_ESP_PWR_EN, true);
        break;
    case ESP_OFF:
        /* with CONFIG switch 1 on this sinks 3.3 mA through R70 (1k): allowed for by the design */
        gpio_output(PIN_ESP_PWR_EN, false);
        break;
    }
    esp_mode_now = mode;
#else
    (void)mode;
#endif
}

enum esp_mode board_esp_get_mode(void)
{
    return esp_mode_now;
}

bool board_esp_rail_up(void)
{
#if BOARD_HAS_ESP32
    /* EN has 10k to 3V3_ESP on the module side of the 1k series resistor: with PB7 an input it reads
     * the rail. (Low also while SW4 is held or the USB bridge holds the ESP32 in reset.) */
    return gpio_read(PIN_ESP_EN_M);
#else
    return false;
#endif
}

void board_esp_reset(void)
{
#if BOARD_HAS_ESP32
    /* REVIEW item 23: "drive ESP_EN open-drain". The latch is low and the pin open drain before it
     * becomes an output; it is never driven high. REVIEW: PB7 goes back to an input afterwards, so the
     * auto-reset circuit of the USB bridge (J7) has the line to itself. */
    gpio_output_open_drain(PIN_ESP_EN_M, false);
    delay_ms(20);
    gpio_write(PIN_ESP_EN_M, true);
    gpio_input(PIN_ESP_EN_M, GPIO_PULL_NONE);
    gpio_set_open_drain(PIN_ESP_EN_M, false);
#endif
}

/* ------------------------------------------------------------------ EXTI
 *
 * REVIEW round 5: "one pin per EXTI line". Line n can be pin n of one port only, so PA15 (USER) and
 * PC15 (accelerometer INT2) cannot both interrupt: INT2 is not used as an interrupt, accelerometer
 * events go to INT1 (PC14). Line 3 (PA3 or PB3) and line 0 (PA0 or PC0) are free here.
 *
 * X(line, pin, rising edge, falling edge, name) */
#if defined(BOARD_REV0)
#define EXTI_OWNERS(X)                                   \
    X(13, PIN_PANIC_BTN, 0, 1, "PANIC button (J9)")      \
    X(14, PIN_ACC_INT1, 1, 0, "accelerometer INT1")      \
    X(15, PIN_BTN_USER, 0, 1, "USER button")
#else
#define EXTI_OWNERS(X) X(13, PIN_PANIC_BTN, 1, 0, "B1 button")
#endif

/* a second entry for a line that already has one declares the same enumerator twice: a compile error */
#define X(line, pin, rising, falling, name) EXTI_LINE_##line##_HAS_ONE_OWNER,
enum { EXTI_OWNERS(X) EXTI_OWNER_COUNT };
#undef X

/* the line number is the pin number, and all of them are served by the one handler below */
#define X(line, pin, rising, falling, name) \
    _Static_assert(PIN_BIT(pin) == (line) && (line) >= 10 && (line) <= 15, "EXTI line " #line);
EXTI_OWNERS(X)
#undef X

struct exti_owner {
    pin_t pin;
    bool rising, falling;
    const char *name;
};

#define X(line, pin, rising, falling, name) { pin, rising, falling, name },
static const struct exti_owner exti_owners[EXTI_OWNER_COUNT] = { EXTI_OWNERS(X) };
#undef X

#define EVENT_DEBOUNCE_MS 50u

static volatile uint32_t event_counts[EXTI_OWNER_COUNT];
static uint32_t event_last_ms[EXTI_OWNER_COUNT];

void EXTI15_10_IRQHandler(void);

void EXTI15_10_IRQHandler(void)
{
    uint32_t pending = EXTI->PR1 & EXTI->IMR1 & 0xFC00u;
    EXTI->PR1 = pending; /* write 1 to clear */
    uint32_t now = millis();
    for (unsigned i = 0; i < EXTI_OWNER_COUNT; i++) {
        if (pending & (1u << PIN_BIT(exti_owners[i].pin))) {
            if ((uint32_t)(now - event_last_ms[i]) >= EVENT_DEBOUNCE_MS) {
                event_last_ms[i] = now;
                event_counts[i]++;
            }
        }
    }
}

void board_events_init(void)
{
    for (unsigned i = 0; i < EXTI_OWNER_COUNT; i++) {
        unsigned line = PIN_BIT(exti_owners[i].pin);
        unsigned shift = 4u * (line & 3u);
        /* SYSCFG_EXTICRn: 0 = port A, 1 = B, 2 = C, the same numbering as PIN_PORT() */
        SYSCFG->EXTICR[line >> 2] =
            (SYSCFG->EXTICR[line >> 2] & ~(15u << shift)) | (PIN_PORT(exti_owners[i].pin) << shift);
        if (exti_owners[i].rising) {
            EXTI->RTSR1 |= 1u << line;
        }
        if (exti_owners[i].falling) {
            EXTI->FTSR1 |= 1u << line;
        }
        EXTI->PR1 = 1u << line;
        EXTI->IMR1 |= 1u << line;
    }
    NVIC_SetPriority(EXTI15_10_IRQn, 3);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

unsigned board_event_sources(void)
{
    return EXTI_OWNER_COUNT;
}

const char *board_event_name(unsigned source)
{
    return source < EXTI_OWNER_COUNT ? exti_owners[source].name : "?";
}

uint32_t board_event_count(unsigned source)
{
    return source < EXTI_OWNER_COUNT ? event_counts[source] : 0;
}

/* ------------------------------------------------------------------ watchdog and reset cause */

void watchdog_start(void)
{
    /* stop the watchdog while a debugger has the core halted, or every breakpoint ends in a reset */
    DBGMCU->APB1FZR1 |= DBGMCU_APB1FZR1_DBG_IWDG_STOP;

    IWDG->KR = 0xCCCCu; /* start (this also starts the 32 kHz LSI) */
    IWDG->KR = 0x5555u; /* unlock PR and RLR */
    IWDG->PR = 3u;      /* LSI / 32 = about 1 kHz */
    IWDG->RLR = 1999u;  /* about 2 s */
    for (uint32_t spin = 0; (IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) && spin < 200000u; spin++) {
    }
    IWDG->KR = 0xAAAAu;
}

void watchdog_feed(void)
{
    IWDG->KR = 0xAAAAu;
}

const char *board_reset_cause(void)
{
    static const char *cause;
    if (cause) {
        return cause;
    }
    uint32_t csr = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;
    /* most specific first: the NRST flag is set by every reset that pulls the pin */
    if (csr & RCC_CSR_IWDGRSTF) {
        cause = "WATCHDOG (the firmware hung or faulted)";
    } else if (csr & RCC_CSR_WWDGRSTF) {
        cause = "window watchdog";
    } else if (csr & RCC_CSR_SFTRSTF) {
        cause = "software reset";
    } else if (csr & RCC_CSR_LPWRRSTF) {
        cause = "low-power reset";
    } else if (csr & RCC_CSR_OBLRSTF) {
        cause = "option byte load";
    } else if (csr & RCC_CSR_BORRSTF) {
        cause = "power-on or brown-out";
    } else if (csr & RCC_CSR_PINRSTF) {
        cause = "NRST pin (RESET button or the debug probe)";
    } else {
        cause = "unknown";
    }
    return cause;
}
