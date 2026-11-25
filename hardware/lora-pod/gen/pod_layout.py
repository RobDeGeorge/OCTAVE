"""Placement for the LoRa pod Rev 0: 95 x 46 mm, 4 layers, every part on top.

Every part has an explicit place: (x, y) is the centre of its courtyard, in mm from the board's
top-left corner, y down. Left to right: the GPS module in the middle of its own 37 x 46 mm of
ground (Quectel asks for 30 x 30 mm, 5 mm to any edge, nothing tall within 10 mm of the patch),
the GPS supply switch / IMU / supercap column, the LoRa + Bluetooth module (its LoRa antenna socket
at the bottom right, away from the GPS), LEDs and buttons, the ranging-radio header down the right
end. Along the bottom edge: SWD, Qwiic, pod bus (J1), USB-C, pod bus (J7), with the END switch above J7. The bench header runs along
the top. Between the module and the bottom connectors: the supply input on the left, the regulator
on the right.
"""

W, H = 95.0, 46.0
HOLES = [(3.0, 3.0), (3.0, H - 3.0), (W - 3.0, 3.0), (W - 3.0, H - 3.0)]    # M2.5
HOLE_KEEP = 2.6
GNSS = (19.0, 23.0)
# the patch antenna's feed point: no copper on any layer (L80-R hardware design, 4.1.2); the
# footprint's own rule area covers F.Cu only. Offset from the module's centre before rotation.
FEED = (-0.6, -0.63, 1.5)       # x, y, radius

_G = GNSS[1] - 20.0             # the GPS's own parts and the column right of it move with the module
MOD = (65.0, 17.0)              # RAK4630: pin 1 top right; lands 14-22 at x - 11.5, 36-44 at x + 11.5

# ref: (x, y, rotation)
PLACE = {
    # GPS and its parts (signal pins 1-6 face right, towards the rest of the board)
    "U4": (GNSS[0], GNSS[1], 180),
    "C14": (30.4, 15.69 + _G, 0), "R15": (33.9, 15.69 + _G, 0),     # V_BCKP: 1u at the pin, 1k from the diode
    "C12": (30.4, 18.23 + _G, 0), "C13": (33.9, 18.23 + _G, 0),     # VCC: 100n, 10u
    "R14": (30.4, 23.31 + _G, 0),                                   # TXD1 series
    "R13": (30.4, 25.85 + _G, 0),                                   # RXD1 series
    # IMU, backup diode, GPS supply switch
    "C16": (40.5, 6.6 + _G, 0), "U6": (40.5, 9.5 + _G, 0), "C17": (40.5, 12.4 + _G, 0),
    "D8": (40.0, 15.2 + _G, 0),
    "U5": (40.0, 20.0 + _G, 180), "C11": (43.6, 20.0 + _G, 90),
    "C10": (40.0, 23.4 + _G, 0), "R12": (40.0, 25.1 + _G, 0),
    "C15": (47.0, 11.0 + _G, 90),                                   # supercap, on edge
    "R8": (46.5, 22.5, 0), "R9": (46.5, 24.2, 0),                   # I2C pull-ups
    # LoRa + Bluetooth module and what sits at its pins
    "U3": (MOD[0], MOD[1], 0),
    "C9": (51.0, 16.0, 90),                                         # NRST 100n (land 17)
    "C8": (51.0, 20.6, 90),                                         # VBAT_SX / VBAT_IO_SX 100n (lands 20, 21)
    "C6": (79.5, 11.3, 90), "C7": (79.5, 14.6, 90),                 # VDD_NRF / VBAT_NRF: 100n, 10u (lands 43, 44)
    "D6": (85.5, 8.0, 0), "R10": (85.5, 9.8, 0),                    # green LED
    "D7": (85.5, 12.2, 0), "R11": (85.5, 14.0, 0),                  # red LED
    "SW2": (85.5, 19.0, 0), "SW1": (85.5, 25.0, 0),                 # PAIR, RESET
    "J4": (65.0, 3.2, 90),                                          # bench header along the top edge
    "J5": (92.6, 23.0, 0),                                          # ranging-radio header down the right end
    # under the module: battery sense divider, UART pull-up, the USB CC resistors
    "R1": (54.0, 27.4, 0), "R2": (57.4, 27.4, 0), "C4": (60.8, 27.4, 0),
    "R5": (65.2, 27.4, 0), "R6": (68.6, 27.4, 0), "R7": (72.0, 27.4, 0),
    # supply input: TVS, fuse, reverse diode; USB ESD and its OR diode
    "D1": (51.5, 31.3, 0), "F1": (58.0, 31.0, 0), "D2": (63.2, 31.0, 0),
    "U2": (69.0, 31.2, 0), "D5": (74.2, 31.0, 0), "C5": (74.2, 34.0, 0),
    # RS-485 driver above the first bus connector, its decoupling and enable resistors left of it
    "U7": (59.6, 35.8, 0), "C18": (54.0, 34.6, 0), "R16": (54.0, 36.4, 0), "R17": (54.0, 38.2, 0),
    # regulator, right of the USB diode
    "C1": (79.6, 31.0, 0), "C2": (79.6, 33.2, 0), "U1": (85.5, 32.0, 0), "C3": (89.8, 32.0, 90),
    # bus terminator and its switch, above the second bus connector
    "R18": (77.2, 37.4, 90), "SW3": (84.0, 36.6, 0),
    # connectors on the bottom edge
    "J3": (40.3, H - 5.0, 0), "J6": (48.6, H - 3.6, 0), "J1": (58.0, H - 3.5, 0), "J2": (69.0, H - 3.9, 0),
    "J7": (82.0, H - 3.5, 0),
}
