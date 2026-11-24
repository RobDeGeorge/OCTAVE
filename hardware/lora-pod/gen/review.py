#!/usr/bin/env python3
"""view/lora-pod-placement-review.png: the top view from `build.py view` with the blocks named.

    python3 review.py        (after build.py view; needs rsvg-convert and Pillow)
"""
import os
import subprocess

from PIL import Image, ImageDraw, ImageFont

import pod_layout as L

VIEW = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "view")
png = os.path.join(VIEW, "lora-pod-top.png")
subprocess.run(["rsvg-convert", "-w", "2800", "-b", "white", os.path.join(VIEW, "lora-pod-top.svg"), "-o", png], check=True)
font = subprocess.run(["fc-match", "-f", "%{file}", "sans:bold"], capture_output=True, text=True).stdout
im = Image.open(png).convert("RGB")
S, PAD = im.width / L.W, 300
out = Image.new("RGB", (im.width + 40, im.height + PAD + 40), "white")
out.paste(im, (20, PAD // 2 + 20))
d = ImageDraw.Draw(out)
F, BLUE, GREEN = ImageFont.truetype(font, 30), (0, 70, 160), (0, 110, 0)
X = lambda x: 20 + x * S                    # noqa: E731
Y = lambda y: PAD // 2 + 20 + y * S         # noqa: E731
gx, gy = L.GNSS
d.rectangle([X(gx - 7.5), Y(gy - 7.5), X(gx + 7.5), Y(gy + 7.5)], outline=(0, 140, 0), width=4)
d.rectangle([X(gx - 17.5), Y(gy - 17.5), X(gx + 17.5), Y(gy + 17.5)], outline=(0, 140, 0), width=2)
d.text((X(4), Y(gy + 10.5)), "green: 15 mm patch antenna, and the\n10 mm zone kept clear of tall parts",
       font=ImageFont.truetype(font, 26), fill=GREEN)
gy0 = gy - 20
TOP = [("GPS  L80-R\n(antenna on top)", 19, gy - 6, 3), ("IMU", 40.5, 8.5 + gy0, 30), ("supercap\n(hand-fit)", 47, 6 + gy0, 36),
       ("BENCH header\n(not fitted)", 62, 2, 47), ("LoRa + Bluetooth  RAK4630\n(antennas: two IPEX on module)", 68, 12, 60)]
BOT = [("GPS power switch\n+ backup diode", 40, 22 + gy0, 8), ("SWD", 40.3, L.H - 3, 23), ("Qwiic", 48.6, L.H - 2, 28),
       ("12 V in: TVS,\nfuse, diode", 54, 31.5, 34), ("VIN / UART cable\nVIN, TX, RX, GND", 58, L.H - 2, 46),
       ("USB-C", 69, L.H - 1.5, 60), ("3V3 regulator\n(40 V input)", 84, 33, 67), ("PAIR, RESET;\nLEDs above", 85.5, 27, 78),
       ("RANGE header\n(not fitted)", 92.6, 33, 88)]
for t, x, y, tx in TOP:
    d.text((X(tx), 10), t, font=F, fill=BLUE)
    d.line([(X(tx) + 60, 90), (X(x), Y(y))], fill=BLUE, width=3)
by = PAD // 2 + 20 + im.height + 50
for t, x, y, tx in BOT:
    d.text((X(tx), by), t, font=F, fill=BLUE)
    d.line([(X(tx) + 60, by - 4), (X(x), Y(y))], fill=BLUE, width=3)
out.save(os.path.join(VIEW, "lora-pod-placement-review.png"))
print(out.size)
