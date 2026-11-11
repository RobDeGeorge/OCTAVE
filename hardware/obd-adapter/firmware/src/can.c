#include "can.h"

#include <stddef.h>

#include "board.h"
#include "can_timing.h"
#include "clock.h"
#include "gpio.h"

/* The G4's message RAM is fixed: 0x350 bytes per controller, in the order FDCAN1, 2, 3 (RM0440,
 * "FDCAN message RAM"). Inside one block:
 *   0x000  28 standard filters      0x070  8 extended filters
 *   0x0B0  Rx FIFO 0: 3 elements    0x188  Rx FIFO 1: 3 elements
 *   0x260  Tx event FIFO            0x278  Tx buffers
 * An element is 72 bytes: two header words and 64 data bytes. */
#define RAM_BLOCK_SIZE  0x350u
#define RAM_RXF0_OFFSET 0x0B0u
#define RAM_ELEMENT     72u

struct bus {
    FDCAN_GlobalTypeDef *can;
    unsigned ram_index;
    IRQn_Type irq;
    pin_t rxd, txd, stb;
    unsigned rxd_af;
};

static const struct bus buses[2] = {
    { FDCAN2, 1, FDCAN2_IT0_IRQn, PIN_CAN1_RXD, PIN_CAN1_TXD, PIN_CAN1_STB, AF_CAN1_RXD },
    { FDCAN3, 2, FDCAN3_IT0_IRQn, PIN_CAN2_RXD, PIN_CAN2_TXD, PIN_CAN2_STB, AF_CAN2_RXD },
};

#define RING_SIZE 64u /* a power of two */

static const struct bus *active;
static struct can_frame ring[RING_SIZE];
static volatile uint32_t ring_head, ring_tail;
static volatile uint32_t n_received, n_dropped, n_lost, n_errors;

static volatile uint32_t *bus_ram(const struct bus *b)
{
    return (volatile uint32_t *)(SRAMCAN_BASE + b->ram_index * RAM_BLOCK_SIZE);
}

static void can_isr(void)
{
    const struct bus *b = active;
    if (!b) {
        return;
    }
    FDCAN_GlobalTypeDef *can = b->can;
    uint32_t ir = can->IR;
    can->IR = ir; /* write 1 to clear */
    if (ir & FDCAN_IR_RF0L) {
        n_lost++;
    }
    if (ir & (FDCAN_IR_PEA | FDCAN_IR_PED)) {
        n_errors++;
    }
    uint32_t now = millis();
    while (can->RXF0S & FDCAN_RXF0S_F0FL) {
        uint32_t index = (can->RXF0S & FDCAN_RXF0S_F0GI) >> FDCAN_RXF0S_F0GI_Pos;
        volatile uint32_t *e =
            (volatile uint32_t *)((uintptr_t)bus_ram(b) + RAM_RXF0_OFFSET + index * RAM_ELEMENT);
        uint32_t r0 = e[0], r1 = e[1];
        n_received++;
        uint32_t next = (ring_head + 1u) & (RING_SIZE - 1u);
        if (next == ring_tail) {
            n_dropped++;
        } else {
            struct can_frame *f = &ring[ring_head];
            f->ms = now;
            f->extended = r0 & (1u << 30);
            f->remote = r0 & (1u << 29);
            f->id = f->extended ? (r0 & 0x1FFFFFFFu) : ((r0 >> 18) & 0x7FFu);
            f->fd = r1 & (1u << 21);
            unsigned dlc = (r1 >> 16) & 15u;
            f->dlc = (uint8_t)(dlc > 8u ? 8u : dlc);
            uint32_t d0 = e[2], d1 = e[3]; /* data bytes in bus order, least significant byte first */
            for (unsigned i = 0; i < 4u; i++) {
                f->data[i] = (uint8_t)(d0 >> (8u * i));
                f->data[4u + i] = (uint8_t)(d1 >> (8u * i));
            }
            ring_head = next;
        }
        can->RXF0A = index; /* hand the element back */
    }
}

void FDCAN2_IT0_IRQHandler(void);
void FDCAN3_IT0_IRQHandler(void);

void FDCAN2_IT0_IRQHandler(void)
{
    can_isr();
}

void FDCAN3_IT0_IRQHandler(void)
{
    can_isr();
}

static bool wait_cccr(FDCAN_GlobalTypeDef *can, uint32_t mask, uint32_t want)
{
    uint32_t start = millis();
    while ((can->CCCR & mask) != want) {
        if ((uint32_t)(millis() - start) > 20u) {
            return false;
        }
    }
    return true;
}

/* the transceiver back to standby, the pins back to their start-up state */
static void bus_release(const struct bus *b)
{
    gpio_output(b->txd, true); /* REVIEW: TXD high before STB changes, and it never was anything else */
    gpio_output(b->stb, true);
    gpio_input(b->rxd, GPIO_PULL_NONE);
}

enum can_start can_listen_start(unsigned bus, uint32_t bitrate)
{
    if (bus < 1u || bus > 2u) {
        return CAN_BAD_BUS;
    }
    struct can_timing timing;
    if (!can_timing_calc(CLOCK_HZ, bitrate, &timing)) {
        return CAN_BAD_BITRATE;
    }
    can_listen_stop();

    const struct bus *b = &buses[bus - 1u];
    FDCAN_GlobalTypeDef *can = b->can;

    RCC->APB1ENR1 |= RCC_APB1ENR1_FDCANEN; /* one clock enable for all three controllers */
    (void)RCC->APB1ENR1;

    /* out of sleep, into initialisation, configuration unlocked */
    can->CCCR &= ~FDCAN_CCCR_CSR;
    if (!wait_cccr(can, FDCAN_CCCR_CSA, 0)) {
        return CAN_NO_INIT;
    }
    can->CCCR |= FDCAN_CCCR_INIT;
    if (!wait_cccr(can, FDCAN_CCCR_INIT, FDCAN_CCCR_INIT)) {
        return CAN_NO_INIT;
    }
    can->CCCR |= FDCAN_CCCR_CCE;

    /* MON = bus monitoring mode (ISO 11898-1): the controller receives, and sends nothing: no
     * acknowledge, no error flag, no frame. No FDOE: classic CAN only, a CAN FD frame on the bus is
     * counted as an error. No DAR / TEST / loop back. */
    can->CCCR = FDCAN_CCCR_INIT | FDCAN_CCCR_CCE | FDCAN_CCCR_MON;

    volatile uint32_t *ram = bus_ram(b);
    for (unsigned i = 0; i < RAM_BLOCK_SIZE / 4u; i++) {
        ram[i] = 0;
    }

    can->NBTP = can_timing_nbtp(&timing);
    /* no filters (LSS = LSE = 0); frames that match none go to FIFO 0 (ANFS = ANFE = 0); remote frames
     * are kept; FIFO 0 blocks when full, so RF0L counts what was lost */
    can->RXGFC = 0;
    can->IE = FDCAN_IE_RF0NE | FDCAN_IE_RF0LE | FDCAN_IE_PEAE | FDCAN_IE_PEDE;
    can->ILS = 0; /* everything on interrupt line 0 */
    can->ILE = FDCAN_ILE_EINT0;

    ring_head = ring_tail = 0;
    n_received = n_dropped = n_lost = n_errors = 0;
    active = b;

    /* Only RXD goes to the controller. TXD stays a GPIO output at high: even a controller that was
     * somehow not in monitoring mode has no pin to transmit with. */
    gpio_output(b->txd, true);
    gpio_alternate(b->rxd, b->rxd_af, GPIO_PULL_NONE, false);

    can->CCCR &= ~FDCAN_CCCR_INIT;
    if (!wait_cccr(can, FDCAN_CCCR_INIT, 0)) {
        active = NULL;
        bus_release(b);
        return CAN_NO_INIT;
    }
    if (!(can->CCCR & FDCAN_CCCR_MON)) {
        can->CCCR |= FDCAN_CCCR_INIT;
        active = NULL;
        bus_release(b);
        return CAN_NOT_MONITOR;
    }

    NVIC_SetPriority(b->irq, 1);
    NVIC_EnableIRQ(b->irq);
    /* REVIEW: "never drive STB low while TXD is low". TXD has been high since reset. */
    gpio_write(b->stb, false); /* transceiver from standby to normal: the receiver comes alive */
    return CAN_OK;
}

void can_listen_stop(void)
{
    const struct bus *b = active;
    if (!b) {
        return;
    }
    NVIC_DisableIRQ(b->irq);
    bus_release(b);
    b->can->IE = 0;
    b->can->CCCR |= FDCAN_CCCR_INIT;
    active = NULL;
}

bool can_listening(void)
{
    return active != NULL;
}

bool can_get(struct can_frame *frame)
{
    if (ring_tail == ring_head) {
        return false;
    }
    *frame = ring[ring_tail];
    ring_tail = (ring_tail + 1u) & (RING_SIZE - 1u);
    return true;
}

void can_get_stats(struct can_stats *stats)
{
    stats->received = n_received;
    stats->dropped = n_dropped;
    stats->lost = n_lost;
    stats->errors = n_errors;
    stats->last_error = 0;
    stats->rx_error_count = 0;
    if (active) {
        stats->last_error = (uint8_t)((active->can->PSR & FDCAN_PSR_LEC) >> FDCAN_PSR_LEC_Pos);
        stats->rx_error_count = (uint8_t)((active->can->ECR & FDCAN_ECR_REC) >> FDCAN_ECR_REC_Pos);
    }
}

const char *can_error_name(unsigned lec)
{
    static const char *const names[8] = {
        "none", "stuff", "form", "no acknowledge", "bit 1", "bit 0", "CRC", "none since last read",
    };
    return names[lec & 7u];
}

const char *can_start_name(enum can_start result)
{
    switch (result) {
    case CAN_OK:          return "ok";
    case CAN_BAD_BUS:     return "no such bus (1 or 2)";
    case CAN_BAD_BITRATE: return "no bit timing for that bit rate from a 16 MHz clock";
    case CAN_NO_INIT:     return "the controller did not answer";
    case CAN_NOT_MONITOR: return "monitoring mode did not read back: refused";
    }
    return "?";
}
