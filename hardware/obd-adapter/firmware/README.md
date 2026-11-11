# OBD adapter firmware (bring-up)

Firmware for the STM32G474RET6 on the OCTAVE OBD adapter, Rev 0. It is a bring-up skeleton: it
puts every pin into a safe state, proves the clock, the LEDs, the buttons, the battery sense, the
I2C parts and the CAN receive path, and gives a serial console to do that from.

**Status: builds, not run on hardware.** No board and no Nucleo was connected when this was written.
Both targets compile with zero warnings and the host tests pass. Everything else on this page is
unproven until someone flashes it. The section "What is untested" lists it line by line.

## What it is

- A reset-safe pin table (`src/safe_pins.c`): the first thing `main()` does, in an order that never
  passes through an unsafe state. Each entry cites the rule in `../verify/REVIEW.md` it implements.
- Clock, 1 ms tick, watchdog, RGB heartbeat, USER button, fault handler that switches every bus
  driver off before it prints anything.
- A console on a UART: `status`, `pins`, `vbat`, `dlc`, `esp`, `i2c scan`, `can listen`, `kline`.
- Two build targets from the same source: `rev0` (the board) and `nucleo` (a NUCLEO-G474RE, so work
  can start before the boards arrive).

## What it is not

- Not an ELM327 replacement. There is no OBD protocol layer, no PID decoding, no AT commands.
- It never transmits on CAN. `can listen` runs the controller in bus monitoring mode and the TXD pin
  is never handed to the controller: no frame, no acknowledge, no error flag.
- No USB. The USB-C socket J2 is only used by the chip's ROM bootloader (DFU) to flash. A USB serial
  console is the first next step; there is no half-finished USB code in the tree.
- No J1850 (VPW / PWM) codec, no K-line initialisation sequences, no QSPI flash, no SD card, no link
  to the ESP32, no low-power modes.

## Decisions made for you

These were chosen to get a first build. Change any of them.

| Decision | Why | Alternative |
|---|---|---|
| Bare-metal C11, no RTOS | A bring-up tool is one loop | Zephyr or FreeRTOS once there is a protocol stack |
| ST's CMSIS device headers, not the HAL | Ten files instead of a few hundred; every register write is visible next to the rule it implements | STM32CubeG4 HAL / LL |
| CMake + `arm-none-eabi-gcc` | Same build tool as the rest of OCTAVE | PlatformIO, STM32CubeIDE |
| 16 MHz system clock, no PLL | HSE and HSI16 are both 16 MHz, so a missing crystal changes nothing but accuracy | PLL to 170 MHz when something needs it |
| Console on a UART for both boards | Works on the Nucleo today through the ST-LINK | USB CDC on J2 (TinyUSB) |
| Not built by the repo's top-level CMake or CI | Needs a cross compiler and a download | Add a CI job later |

## Dependencies

| What | Version | Licence | Where from |
|---|---|---|---|
| `arm-none-eabi-gcc`, binutils, newlib (nano) | built with GCC 16.2.0 | GPL / BSD-style (toolchain) | distribution packages (`arm-none-eabi-gcc`, `arm-none-eabi-newlib`) |
| CMake | 3.21 or newer | BSD-3-Clause | distribution |
| ST CMSIS device files for the G4: `stm32g4xx.h`, `stm32g474xx.h`, `system_stm32g4xx.h`, `startup_stm32g474xx.s` | v1.2.6 (commit `25664dd`) | Apache-2.0 | github.com/STMicroelectronics/cmsis-device-g4 |
| Arm CMSIS-Core: `core_cm4.h`, `cmsis_version.h`, `cmsis_compiler.h`, `cmsis_gcc.h`, `mpu_armv7.h` | v5.9.0 (commit `1a2f783`) | Apache-2.0 | github.com/STMicroelectronics/cmsis-core |
| Host tests | system C compiler, Python 3 with KiCad's `pcbnew` for the pin map check | | |

The CMSIS files are not in the repository. The first configure downloads those ten files (the nine
above and ST's `LICENSE.md`) from the pinned commits into `build/arm/deps/` and checks each against
a SHA-256 written in `CMakeLists.txt`. A file that is already there with the right hash is not
downloaded again, so an offline build works after the first one; `-DFW_DEPS_DIR=<dir>` points the
build at a copy kept elsewhere. Apache-2.0 code may be combined into this GPL-3.0-or-later project.
If a firmware binary is ever distributed, add the two packages to the repository's `NOTICE`.

## Build

```bash
cd hardware/obd-adapter/firmware

cmake --preset arm            # downloads the ten CMSIS files on the first run
cmake --build --preset arm    # build/arm/obd-adapter-rev0.{elf,bin}, obd-adapter-nucleo.{elf,bin}

cmake --preset host           # the pure logic, system compiler, ASan + UBSan
cmake --build --preset host
ctest --preset host
```

Warnings are errors (`-Wall -Wextra -Wshadow -Wundef -Wstrict-prototypes -Wmissing-prototypes`,
`-DFW_WERROR=OFF` to relax). Size at the time of writing: rev0 17.4 KB of flash, nucleo 16.4 KB, about
1.9 KB of RAM plus a 4 KB stack reserve.

### The pin map is generated

`src/board_pins.h` is written by `tools/gen_pins.py` from the board generator
(`../gen/obd_adapter.py`: which net is on which pin, the battery divider, the crystal) and from the
pin check record (`../verify/pins/pincheck.md`: the alternate function numbers). No alternate
function number is typed by hand anywhere.

```bash
python3 tools/gen_pins.py            # rewrite src/board_pins.h after a board change
python3 tools/gen_pins.py --check    # exit 1 if it is stale; ctest runs this
```

Use the system `python3` (the generator imports KiCad). When the board changes:

1. Re-run `../verify/pins/check.py`, then `python3 tools/gen_pins.py`.
2. `ctest --preset host`. The `pins` test fails if a net appeared or disappeared and
   `src/safe_pins.c` does not say what its start-up state is. A renamed net fails the build.

## Flash

### Rev 0 over USB (DFU, no programmer)

1. Unplug J7 (the ESP32's USB socket) and set CONFIG switch 1 (ESP_ON) off. The ROM bootloader also
   listens on USART2 and I2C4, pins the ESP32 is connected to: a running ESP32 can be mistaken for a
   host.
2. Set CONFIG switch 4 (BOOT0) on.
3. Plug a USB cable into J2. Press RESET (SW1) if the board was already powered.
4. `dfu-util -l` shows `0483:df11`. Then:
   ```bash
   dfu-util -a 0 -s 0x08000000:leave -D build/arm/obd-adapter-rev0.bin
   ```
5. Set switch 4 off again and press RESET. With switch 4 on, every reset ends in the bootloader; the
   firmware says so in its banner and in `status`.

### Rev 0 over SWD (J1, 2x5 1.27 mm Cortex debug header)

```bash
st-flash --reset write build/arm/obd-adapter-rev0.bin 0x08000000
# or
openocd -f interface/stlink.cfg -f target/stm32g4x.cfg \
        -c "program build/arm/obd-adapter-rev0.elf verify reset exit"
```

The watchdog is frozen while a debugger has the core halted.

### NUCLEO-G474RE

```bash
st-flash --reset write build/arm/obd-adapter-nucleo.bin 0x08000000
```

or copy `obd-adapter-nucleo.bin` onto the Nucleo's USB drive.

Never flash the `nucleo` image onto the Rev 0 board: it drives PA2 as a UART output, which on the
board is a line into the ESP32 that must stay an input until the ESP32 is powered.

## Console

115200 baud, 8N1, no flow control. Line editing is backspace and Ctrl-C.

| Board | UART | Where |
|---|---|---|
| Nucleo | USART2, PA2 / PA3 | the ST-LINK's virtual COM port (`/dev/ttyACM0`), nothing to wire |
| Rev 0 | USART1, PC4 / PC5 | header J5 (EXP): pin 5 = TX (out of the board), pin 6 = RX, pin 3, 4 or 14 = GND. 3.3 V levels. |

J5 is not fitted on Rev 0 (DNP). Until USB CDC exists, the console on the board needs that header
or three wires soldered to its pads, and a 3.3 V USB serial adapter. Without it the board still shows
the lamp test and the heartbeat, and SWD works.

```
help                              this list
status                            clock, reset cause, battery, switches, counters
pins                              every pin: its start-up state and what it is now
vbat                              battery voltage at J1962 pin 16
dlc [on|off]                      battery to the J1850 / K-line side (DLC_SW)
esp [auto|on|off|reset]           ESP32 power (PB6) and reset (EN)
i2c scan                          scan the Qwiic bus; expects 0x19 and 0x60
can listen [1|2] [bitrate]        print CAN frames; never transmits or acknowledges
kline listen | kline send <hex>   K line at 10400 baud; send needs `arm`
arm / disarm                      allow `kline send` for two minutes
reset                             restart the firmware
```

`can listen` defaults to bus 1 at 500000 bit/s. Bus 1 is J1962 pins 6 / 14 (the OBD CAN bus), bus 2
is pins 3 / 11. Any key stops it and prints the counters.

### LEDs

| What you see | Meaning |
|---|---|
| red, green, blue, 150 ms each | start-up lamp test |
| green flash once a second | running; Rev 0: on the crystal |
| red flash once a second | Rev 0 only: the 16 MHz crystal did not start, or stopped. Running on the internal oscillator. |
| blue while the USER button is held | button works |
| blue flicker | `can listen` is receiving frames |
| red, steady | fault. Bus drivers are off; the watchdog resets the chip within 2 s. |

On the Nucleo the single LED LD2 lights for any colour.

## Safety rules and where they live

From "Firmware obligations (all rounds)" in `../verify/REVIEW.md`.

| Rule | Where | State |
|---|---|---|
| PA1 / PA2 / PB5 / PB7 low or inputs until the ESP32 is powered | `safe_pins.c`: all six ESP32 lines are inputs; nothing makes them outputs | implemented, host-tested |
| ESP_EN open drain | `board_esp_reset()` in `board.c` | implemented |
| PB5 and PB7 inputs while the ESP32 is flashed through J7 | same: they are inputs except for the 20 ms of `esp reset` | implemented |
| PB6 cannot switch the ESP32 off while J7 is plugged | `esp off` reports "still powered" when EN stays high | reported, cannot be changed in firmware |
| LPUART1 TXINV before PC1 goes to its alternate function | `kline_start()` / `kline_send()` in `kline.c` | implemented |
| `PWR_CR3.UCPD_DBDIS` early | `SystemInit()` in `system.c`, before `.data` is copied | implemented |
| CAN TXD high before STB low; never STB low with TXD low | `safe_pins.c` (TXD high, then STB high), `can.c` (TXD is a GPIO at high for the whole life of the firmware) | implemented, order host-tested |
| STB high (standby) by default | `safe_pins.c`; `can listen` lowers it and raises it again on exit; the fault handler raises it | implemented |
| DLC_SW off, J1850 and K / L drivers idle | first entries of `safe_pins.c`; `board_force_safe()` | implemented, host-tested |
| PC0 pull-up before LPUART1's receiver | `safe_pins.c` (on from the start), kept in `kline_start()` | implemented, host-tested |
| PA15 pull-up stays on as a GPIO | `safe_pins.c` | implemented, host-tested |
| One pin per EXTI line | `EXTI_OWNERS` in `board.c`: a second owner for a line does not compile. Line 15 = USER button, so accelerometer events must be routed to INT1 (PC14, line 14). | implemented |
| ROM bootloader with the ESP32 off | flash instructions above | procedure |
| ATECC608B wakes on SDA low; send it back to sleep | `i2c scan` wakes it, then sends the sleep command | implemented, timing from memory |
| No 32 kHz crystal, RTC on LSI | no RTC here; PC14 / PC15 are inputs | not applicable yet |
| W25Q128 QE bit before quad mode | no QSPI driver | next step |
| Abort a J1850 transmit on the first echo mismatch; filter the comparator capture | no J1850 codec. `kline send` already stops on the first echo mismatch. | next step |
| ESP_SD_CD needs the ESP32's pull-up | ESP32 firmware, not this one | not applicable |

On top of the review's rules:

- At start-up every push-pull output is read back. A pin that reads the other level than it drives
  is printed as `PIN FAULT` (a short or a solder bridge). `pins` shows the same at any time.
- The watchdog (about 2 s) starts before the console does and cannot be stopped.
- A hard fault calls `board_force_safe()` first. The reset state of the chip is safe by the board's
  own pull-downs, so a watchdog reset is too.

## Bring-up order for a new board

Do not plug a new board into a car. Do the whole list on the bench first.

1. **Look.** Solder bridges on U1 (the STM32), the USB sockets and the OBD plug. Orientation of the
   diodes, the TVS and the electrolytic or polymer capacitors.
2. **Resistance.** With nothing connected: TP1 (VBAT_P) to TP4 (GND), TP2 (5V) to GND, TP3 (3V3) to
   GND. None may be a short.
3. **USB power first, current limited.** All CONFIG switches off. Power J2 from a USB port or a
   bench supply set to 5 V, 100 mA limit. Measure 3.3 V at TP3. Expect a few tens of mA with a blank
   chip. Stop if it is more than 100 mA or if anything gets warm.
4. **Flash.** DFU through J2 (switch 4) or SWD on J1. A blank chip enumerates as `0483:df11` with
   switch 4 on; that alone proves the 3.3 V rail, the STM32 and the USB socket.
5. **Watch the LEDs.** Red, green, blue, then a flash once a second. Green = the crystal runs. Red =
   it does not (check Y1 and its two capacitors); everything except CAN accuracy still works.
6. **Console** on J5. Read the banner: clock, reset cause, any `PIN FAULT` line.
7. `pins`. Every output must read the level it drives. `status`.
8. `i2c scan`: 0x19 (WHO_AM_I 0x33) and 0x60.
9. `esp on`, `esp off`, `esp reset`, `esp auto` with CONFIG switch 1 in both positions. Watch the
   3V3_ESP rail and the EN line.
10. **12 V, current limited.** Bench supply at 12.0 V, 200 mA limit, on J1962 pin 16 (+) and pins
    4 / 5 (ground). Measure 5 V at TP2 (the design value of the buck converter is in the board
    README). `vbat` must agree with a multimeter within about 2 %. Sweep the supply from 9 V to 16 V.
11. `dlc on`: TP5 goes to battery voltage, TP8 to the J1850 supply; TP6, TP7, TP9, TP10 stay at
    their idle levels. `dlc off`: all back to zero. Nothing else changes.
12. **CAN on the bench.** A second CAN node that transmits and a third that acknowledges (or one USB
    CAN adapter in normal mode plus another node), 120 ohm at both ends (CONFIG switch 2 is one of
    them). `can listen 1 500000`. A lone transmitter with only this board on the bus shows errors
    and no frames: nobody acknowledges, by design.
13. **In a car.** Engine off, ignition on. `vbat`, then `can listen`. Listen only. Stop here until
    the transmit path has been reviewed and tested on the bench.
14. K-line and J1850 one at a time, on the bench first, with a resistor to 12 V standing in for the
    car. `kline listen` before any `kline send`.

## On a NUCLEO-G474RE

The `nucleo` target runs on a bare Nucleo with nothing attached: console, heartbeat on LD2, button
B1, watchdog, `status`, `pins`. The Nucleo's own parts were checked against the board's pin map so
that no pin fights: PA5 is LD2 (EXP_PA5 on the board, unused there), PC13 is B1 (PANIC_BTN on the
board; active high here, active low there), PA2 / PA3 are the ST-LINK serial port (the ESP32 link on
the board), PC14 / PC15 are the 32 kHz crystal (left alone), PB3 is also the ST-LINK's SWO input.

With breakouts, the same code paths as on the board:

| Function | Nucleo pins | Needs |
|---|---|---|
| `i2c scan` | D7 = PA8 (SDA), D8 = PA9 (SCL) | any 3.3 V I2C breakout. Internal pull-ups are on in this target. |
| `vbat` | A2 = PA4 | a voltage of 0 to 3.3 V. The reading is 11 times the pin voltage. Floating, it reads noise. |
| `can listen 1` | PB12 = RXD, PB13 = TXD, PB14 = STB (morpho header, see UM2505) | a 3.3 V CAN transceiver breakout (SN65HVD230, TCAN332, or a TCAN1044V with VIO). Only RXD is needed; TXD stays high. STB is driven low while listening: tie the breakout's standby / RS pin to it or to ground. |
| `can listen 2` | D3 = PB3 (RXD), D5 = PB4 (TXD), PB15 (STB) | the same |
| `kline listen` / `send` | A5 = PC0 (RX), A4 = PC1 (TX, high = pull the line low) | a transistor stage like the board's, or an L9637 / SN65HVDA195 breakout with the TX polarity in mind |
| `dlc`, J1850 pins | PC3, PC8, PC9, PB2 | a scope or a multimeter: the levels can be measured on the headers |
| `esp ...` | | not available in this target |

Not exercised on a Nucleo: the 16 MHz crystal and its fallback (the Nucleo target stays on HSI16),
the battery divider, the ESP32 power logic, everything behind DLC_SW.

## What is untested

Everything that needs hardware. In particular:

- Nothing has run on a chip. The start-up code, the linker script, the clock switch and every driver
  are compiled and read through, no more.
- **Register details written from memory, not checked against the reference manual in this
  session:** the I2C `TIMINGR` value `0x30420F13` (100 kHz from 16 MHz), the ADC start-up sequence
  and sampling time, the FDCAN message RAM layout (0x350 bytes per controller, Rx FIFO 0 at offset
  0xB0, 72-byte elements) and the meaning of `RXGFC = 0`, the LPUART `BRR` formula, the IWDG
  prescaler code. Register and bit *names* are all from ST's header and compile; what is unverified
  is the behaviour.
- **Part details from memory:** ATECC608B wake (SDA low 60 us, then 1.5 ms) and sleep (word address
  0x01), LIS2DH12 WHO_AM_I = 0x33 at register 0x0F, ISO 14230 inter-byte time 5 ms.
- The RAM region is 96 KB (SRAM1 + SRAM2 at 0x20000000). The 32 KB of CCM SRAM at 0x10000000 is
  declared and unused. ST's own linker script uses one 128 KB region; that relies on an alias of the
  CCM that was not verified here, so it is not used.
- HSI16 is about 1 % accurate. CAN wants better. On the Nucleo target and on a board whose crystal
  failed, `can listen` may show errors on a bus that is fine. It prints a note.
- A 115200 baud console prints about 250 frames a second. A busy bus has several thousand.
  `can listen` counts what it could not print ("not printed") separately from what the controller
  lost. It is a check that the bus is alive, not a logger.
- `vbat` assumes the 3.3 V rail is exactly 3.300 V. The divider is 0.1 %; the regulator is not.
- The divider is after the input protection (`VBAT_F`): on USB power alone `vbat` reads about zero.
- The CAN transceivers are powered from the 5 V buck converter: on USB power alone `can listen`
  receives nothing. It prints a note when the battery voltage is low.
- `board_esp_rail_up()` infers "ESP32 powered" from the EN line reading high through its 10k pull-up
  to 3V3_ESP. It also reads low while the ESP32's reset button is held.
- `kline send` has an interlock (`arm`, `dlc on`, line idle, echo check on every byte) but has never
  driven a line. Bench first.

## Next steps, in order

1. **Run it.** Nucleo first (an hour), then the first Rev 0 board through the bring-up list. Fix
   what the list above got wrong.
2. **USB CDC console on J2** with TinyUSB (MIT), pinned the same way as CMSIS. Removes the need for
   J5 and a serial adapter. HSI48 with CRS as the USB clock, so it does not depend on the crystal.
3. **A `dfu` command** that jumps to the ROM bootloader, so CONFIG switch 4 is only needed for a
   blank or broken chip.
4. **VDDA from VREFINT** (ADC1), so `vbat` does not assume 3.300 V.
5. **CAN transmit**, behind an interlock like `arm`: ISO 15765-4 functional request `7DF`, the
   responses, then ISO-TP. This is the first step that can disturb a car; it wants a review of its
   own and a bench test against a second node before a vehicle.
6. **K-line initialisation**: fast init (25 ms low, 25 ms high, StartCommunication) and 5-baud init,
   with the L line.
7. **J1850 VPW, then PWM**, on TIM3 (input capture on PC6 / PC7, drive on PC8 / PC9). The symbol
   timing tables must come from SAE J1850 itself, not from memory, and the codec gets host tests
   before it gets a pin. The two review rules (abort on the first echo mismatch, filter the
   comparator capture) apply here.
8. **The link to the ESP32** (USART2 with flow control), honouring the power rules: UART pins to
   their alternate function only while `board_esp_rail_up()`.
9. QSPI flash (set the QE bit first), accelerometer wake on INT1, low-power modes for a parked car.
10. A CI job that builds both targets and runs the host tests, once the code is worth protecting.

## Layout

```
CMakeLists.txt, CMakePresets.json   two presets: arm (firmware), host (tests)
cmake/arm-none-eabi.cmake           toolchain file
tools/gen_pins.py                   writes src/board_pins.h from the board design
src/board_pins.h                    generated pin map
src/safe_pins.{h,c}                 the start-up state of every pin, with the reasons
src/board.{h,c}                     applies it; LEDs, buttons, DLC_SW, ESP32 power, EXTI, watchdog
src/system.c, stm32g474re.ld        earliest start-up, memory map
src/clock.{h,c}                     crystal with fallback, 1 ms tick
src/uart.{h,c}                      console port
src/adc.{h,c}, vbat.{h,c}           battery voltage (vbat is pure, host-tested)
src/i2c.{h,c}                       Qwiic bus
src/can.{h,c}, can_timing.{h,c}     listen-only CAN (can_timing is pure, host-tested)
src/kline.{h,c}                     K line
src/console.{h,c}, fmt.{h,c}        line editor, parser, printf (pure, host-tested)
src/cmds.{h,c}, main.c              the commands, the main loop, the fault handler
tests/                              host tests
```
