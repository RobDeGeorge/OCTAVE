/* Everything about the board that is not a bus: the start-up pin state, LEDs, buttons, the legacy-bus
 * supply switch, the ESP32's power and reset, the watchdog, the reset cause. */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "board_pins.h"
#include "safe_pins.h"

#if defined(BOARD_REV0) == defined(BOARD_NUCLEO)
#error "build with exactly one of BOARD_REV0, BOARD_NUCLEO (the CMake targets do)"
#endif

#if defined(BOARD_REV0)
#define THIS_BOARD      BOARD_ID_REV0
#define BOARD_HAS_ESP32 1
#else
#define THIS_BOARD      BOARD_ID_NUCLEO
#define BOARD_HAS_ESP32 0
#endif

/* Applies safe_pins[] top to bottom. The first thing main() does. */
void board_init_safe(void);

/* Bus drivers off, CAN transceivers to standby, DLC_SW off: from any context, at any time. */
void board_force_safe(void);

/* Calls `report` for every output pin whose level at the pin is not the level it drives (a short, a
 * solder bridge, a part fitted the wrong way round). Returns how many. */
unsigned board_check_outputs(void (*report)(const struct safe_pin *pin, bool driven, bool read));

void led_set(bool red, bool green, bool blue);

/* USER button (Rev 0: SW3 on PA15) or B1 (Nucleo: PC13) held down right now */
bool board_button(void);

/* CONFIG switch 4 on: the next reset starts the ROM bootloader instead of this firmware */
bool board_boot0_switch(void);

/* DLC_SW: battery to the J1850 supply, the comparators and the K / L line pull-ups */
void board_dlc(bool on);
bool board_dlc_is_on(void);

enum esp_mode {
    ESP_AUTO, /* PB6 is an input: CONFIG switch 1 (or a cable in J7) decides */
    ESP_ON,   /* PB6 driven high */
    ESP_OFF,  /* PB6 driven low: overrides switch 1, not a cable in J7 */
};
void board_esp_mode(enum esp_mode mode);
enum esp_mode board_esp_get_mode(void);
bool board_esp_rail_up(void); /* EN reads high: 3V3_ESP is up and nothing holds the ESP32 in reset */
void board_esp_reset(void);   /* pulls EN low for 20 ms, open drain */

/* button and sensor interrupts: one counter per source */
void board_events_init(void);
unsigned board_event_sources(void);
const char *board_event_name(unsigned source);
uint32_t board_event_count(unsigned source);

/* independent watchdog, about 2 s. Once started it cannot be stopped. */
void watchdog_start(void);
void watchdog_feed(void);

/* why the chip last reset; reads and clears the flags on the first call */
const char *board_reset_cause(void);

#endif /* BOARD_H */
