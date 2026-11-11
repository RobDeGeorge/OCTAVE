#include "kline.h"

#include "board.h"
#include "clock.h"
#include "gpio.h"

#define KLINE_BAUD 10400u
/* LPUART: BRR = 256 x fck / baud. 256 x 16 MHz / 10400 = 393846 (10400.004 baud). */
#define KLINE_BRR  ((uint32_t)(((uint64_t)256u * CLOCK_HZ + KLINE_BAUD / 2u) / KLINE_BAUD))

#define ECHO_TIMEOUT_MS 4u /* a byte takes 0.96 ms */
#define BYTE_GAP_MS     5u /* ISO 14230-2 P4, tester inter-byte time: 5 ms minimum (from memory) */

#define RX_SIZE 256u /* a power of two */

static volatile uint8_t rx_buf[RX_SIZE];
static volatile uint32_t rx_head, rx_tail;
static volatile uint32_t rx_errors;
static bool running;

void LPUART1_IRQHandler(void);

void LPUART1_IRQHandler(void)
{
    uint32_t isr = LPUART1->ISR;
    if (isr & USART_ISR_RXNE) {
        uint8_t c = (uint8_t)LPUART1->RDR;
        uint32_t next = (rx_head + 1u) & (RX_SIZE - 1u);
        if (next != rx_tail) {
            rx_buf[rx_head] = c;
            rx_head = next;
        } else {
            rx_errors++;
        }
    }
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        LPUART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
        rx_errors++;
    }
}

void kline_start(void)
{
    if (running) {
        return;
    }
    RCC->APB1ENR2 |= RCC_APB1ENR2_LPUART1EN; /* kernel clock: PCLK1, the reset choice */
    (void)RCC->APB1ENR2;

    LPUART1->CR1 = 0; /* UE = 0: CR2 can only be written while the UART is disabled */
    /* REVIEW: "set LPUART1 TXINV before switching PC1 to its alternate function". KLINE_TX high pulls
     * the K line low, so an idle transmitter must hold the pin LOW: without TXINV the UART's idle level
     * (high) would hold the car's K line down for as long as the pin is in AF. The receiver is not
     * inverted: the comparator stage gives K high = pin high. */
    LPUART1->CR2 = USART_CR2_TXINV;
    LPUART1->CR3 = 0;
    LPUART1->BRR = KLINE_BRR;

    /* REVIEW round 6: "PC0 pull-up before enabling LPUART1's receiver": the receive stage is open
     * collector and this pull-up is its only load. */
    gpio_alternate(PIN_KLINE_RX, AF_KLINE_RX, GPIO_PULL_UP, false);

    rx_head = rx_tail = 0;
    rx_errors = 0;
    LPUART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    LPUART1->CR1 = USART_CR1_UE | USART_CR1_RE | USART_CR1_RXNEIE; /* no TE: PC1 is still a low GPIO */
    NVIC_SetPriority(LPUART1_IRQn, 1);
    NVIC_EnableIRQ(LPUART1_IRQn);
    running = true;
}

static void tx_release(void)
{
    /* the pin goes back to a GPIO at low (line released) before the transmitter is switched off */
    gpio_output(PIN_KLINE_TX, false);
    LPUART1->CR1 &= ~USART_CR1_TE;
}

void kline_stop(void)
{
    if (!running) {
        return;
    }
    tx_release();
    NVIC_DisableIRQ(LPUART1_IRQn);
    LPUART1->CR1 = 0;
    gpio_input(PIN_KLINE_RX, GPIO_PULL_UP); /* its start-up state */
    running = false;
}

bool kline_line_high(void)
{
    return gpio_read(PIN_KLINE_RX);
}

int kline_getc(void)
{
    if (rx_tail == rx_head) {
        return -1;
    }
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1u) & (RX_SIZE - 1u);
    return c;
}

uint32_t kline_errors(void)
{
    return rx_errors;
}

int kline_send(const uint8_t *data, unsigned len)
{
    if (!running || !kline_line_high()) {
        return 0;
    }
    while (kline_getc() >= 0) {
    }

    /* Transmitter on while PC1 is still a GPIO: it sends one idle frame, which with TXINV is "pin low".
     * Only then does the pin change hands, from a low GPIO to a low UART output. */
    LPUART1->CR1 |= USART_CR1_TE;
    uint32_t start = millis();
    while (!(LPUART1->ISR & USART_ISR_TEACK) && (uint32_t)(millis() - start) < 5u) {
    }
    delay_ms(2); /* the idle frame: 10 bits = 0.96 ms */
    gpio_alternate(PIN_KLINE_TX, AF_KLINE_TX, GPIO_PULL_NONE, false);

    unsigned sent = 0;
    for (; sent < len; sent++) {
        LPUART1->TDR = data[sent];
        start = millis();
        int echo;
        while ((echo = kline_getc()) < 0 && (uint32_t)(millis() - start) < ECHO_TIMEOUT_MS) {
            watchdog_feed();
        }
        /* the line is one wire: what we send, we hear. Anything else is a collision or a fault, and the
         * only correct reaction is to let go at once. */
        if (echo != data[sent]) {
            break;
        }
        delay_ms(BYTE_GAP_MS);
    }
    tx_release();
    return (int)sent;
}
