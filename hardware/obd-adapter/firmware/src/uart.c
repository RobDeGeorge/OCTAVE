#include "uart.h"

#include <stdarg.h>

#include "board.h"
#include "clock.h"
#include "fmt.h"
#include "gpio.h"

#define CONSOLE_BAUD 115200u

#if defined(BOARD_NUCLEO)
/* PA2 / PA3 are wired to the ST-LINK on the Nucleo. On Rev 0 the same two pins are the ESP32 link,
 * which is why the pin names say ESP. */
#define CONSOLE_UART       USART2
#define CONSOLE_IRQn       USART2_IRQn
#define CONSOLE_IRQHandler USART2_IRQHandler
#define CONSOLE_TX_PIN     PIN_MCU_TX_ESP_M
#define CONSOLE_TX_AF      AF_MCU_TX_ESP_M
#define CONSOLE_RX_PIN     PIN_ESP_TX_MCU
#define CONSOLE_RX_AF      AF_ESP_TX_MCU
#else
#define CONSOLE_UART       USART1
#define CONSOLE_IRQn       USART1_IRQn
#define CONSOLE_IRQHandler USART1_IRQHandler
#define CONSOLE_TX_PIN     PIN_EXP_TX
#define CONSOLE_TX_AF      AF_EXP_TX
#define CONSOLE_RX_PIN     PIN_EXP_RX
#define CONSOLE_RX_AF      AF_EXP_RX
#endif

#define RX_SIZE 128u /* a power of two */

static volatile uint8_t rx_buf[RX_SIZE];
static volatile uint32_t rx_head, rx_tail; /* head: written by the interrupt, tail: by uart_getc() */
static volatile uint32_t rx_lost;

void CONSOLE_IRQHandler(void);

void CONSOLE_IRQHandler(void)
{
    uint32_t isr = CONSOLE_UART->ISR;
    if (isr & USART_ISR_RXNE) {
        uint8_t c = (uint8_t)CONSOLE_UART->RDR; /* reading clears RXNE */
        uint32_t next = (rx_head + 1u) & (RX_SIZE - 1u);
        if (next == rx_tail) {
            rx_lost++;
        } else {
            rx_buf[rx_head] = c;
            rx_head = next;
        }
    }
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        /* an overrun keeps the interrupt pending until it is cleared */
        CONSOLE_UART->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
        if (isr & USART_ISR_ORE) {
            rx_lost++;
        }
    }
}

void uart_init(void)
{
#if defined(BOARD_NUCLEO)
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    (void)RCC->APB1ENR1;
#else
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
#endif
    CONSOLE_UART->CR1 = 0;
    CONSOLE_UART->BRR = (CLOCK_HZ + CONSOLE_BAUD / 2u) / CONSOLE_BAUD; /* oversampling by 16 */
    CONSOLE_UART->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
    /* the transmitter is already idling high when the pin is handed over. RX gets a pull-up so that a
     * header with nothing plugged in reads as an idle line and not as noise. */
    gpio_alternate(CONSOLE_TX_PIN, CONSOLE_TX_AF, GPIO_PULL_NONE, false);
    gpio_alternate(CONSOLE_RX_PIN, CONSOLE_RX_AF, GPIO_PULL_UP, false);
    NVIC_SetPriority(CONSOLE_IRQn, 2);
    NVIC_EnableIRQ(CONSOLE_IRQn);
}

static void put_raw(char c)
{
    /* a character takes 87 us; the bound only keeps a dead UART from hanging the firmware */
    for (uint32_t spin = 0; !(CONSOLE_UART->ISR & USART_ISR_TXE) && spin < 100000u; spin++) {
    }
    CONSOLE_UART->TDR = (uint8_t)c;
}

void uart_putc(char c)
{
    if (c == '\n') {
        put_raw('\r');
    }
    put_raw(c);
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

void uart_flush(void)
{
    for (uint32_t spin = 0; !(CONSOLE_UART->ISR & USART_ISR_TC) && spin < 100000u; spin++) {
    }
}

int uart_getc(void)
{
    if (rx_tail == rx_head) {
        return -1;
    }
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1u) & (RX_SIZE - 1u);
    return c;
}

uint32_t uart_overruns(void)
{
    return rx_lost;
}

static void put_cb(char c, void *ctx)
{
    (void)ctx;
    uart_putc(c);
}

void kprintf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    fmt_vformat(put_cb, NULL, format, ap);
    va_end(ap);
}
