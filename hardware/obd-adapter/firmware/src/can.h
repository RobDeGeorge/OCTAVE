/* CAN, receive only. The controller runs in bus monitoring mode and the TXD pin is never given to it:
 * the adapter cannot transmit, acknowledge or send an error frame.
 *   bus 1 = FDCAN2 = CAN1 on the board = J1962 pins 6 / 14 (the OBD CAN bus)
 *   bus 2 = FDCAN3 = CAN2 on the board = J1962 pins 3 / 11 (a second bus on some cars) */
#ifndef CAN_H
#define CAN_H

#include <stdbool.h>
#include <stdint.h>

struct can_frame {
    uint32_t ms;      /* millis() when the frame was taken from the controller */
    uint32_t id;
    bool extended;    /* 29-bit identifier */
    bool remote;
    bool fd;          /* a CAN FD frame: cannot happen while FD operation is off; kept for the day it is on */
    uint8_t dlc;      /* 0..8 */
    uint8_t data[8];
};

struct can_stats {
    uint32_t received;     /* frames taken from the controller */
    uint32_t dropped;      /* received but not printed: the console was slower than the bus */
    uint32_t lost;         /* the controller's own FIFO overflowed */
    uint32_t errors;       /* protocol errors seen on the bus */
    uint8_t last_error;    /* LEC of the last one, see can_error_name() */
    uint8_t rx_error_count;
};

enum can_start {
    CAN_OK,
    CAN_BAD_BUS,
    CAN_BAD_BITRATE,   /* no bit timing for it from this clock */
    CAN_NO_INIT,       /* the controller did not enter or leave initialisation */
    CAN_NOT_MONITOR,   /* the monitor bit did not read back: refused, transceiver left in standby */
};

enum can_start can_listen_start(unsigned bus, uint32_t bitrate);
void can_listen_stop(void);
bool can_listening(void);
bool can_get(struct can_frame *frame);
void can_get_stats(struct can_stats *stats);
const char *can_error_name(unsigned lec);
const char *can_start_name(enum can_start result);

#endif /* CAN_H */
