"""Pick-and-place corrections for JLCPCB assembly of the volume knob, keyed by LCSC number.

JLCPCB places each part by its own (EasyEDA) footprint, whose zero rotation and origin differ from
KiCad's for many packages. `JLC_CORR[lcsc]` is `(rotation to add in degrees, dx, dy)`, the convention of
`hardware/obd-adapter/gen/build.py` (`JLC_CORR` and `fab()` there): the rotation is added to the one in
KiCad's position file, and (dx, dy) is where JLCPCB's footprint origin sits in the KiCad footprint's own
frame (as drawn in the footprint editor: x right, y down, mm). A part that is not in the table needs no
correction, (0, 0, 0).

How it was derived (2026-11-22, numerically, nothing taken from package conventions):

1. The EasyEDA / LCSC footprint of every LCSC number in the BOM was fetched with easyeda2kicad (which keeps
   EasyEDA's footprint origin and axes) and its pads were laid over the KiCad footprint's pads by pad
   name: K = R(theta) E + t for theta in 0 / 90 / 180 / 270, counter-clockwise as drawn. The theta with
   the smallest largest pad-centre distance is the rotation to add; t is (dx, dy). Where both footprints
   have drilled features (the USB-C's shell pegs and locating holes, the encoder's bosses) t is taken from
   those, as the pegs are what locates the part; otherwise it is the per-axis median over the pads, so
   that pads which only differ in length do not pull the origin. Offsets under 0.05 mm are pad-size
   artefacts and are written as 0.
2. The "residual" in each comment is the largest pad-centre distance left after the fit. The next-best
   rotation was at least 0.63 mm (two-pad 0402) and over 1.2 mm for every polarised part, so no fit was
   ambiguous.
3. Each EasyEDA symbol's pin names were compared with the KiCad symbol's, pad number by pad number, for
   every part with more than two pads: they all agree (so "pad 1 on pad 1" also means the same signal).
4. Sanity check against the sister table, whose entries were checked on a real order: the same method
   gives (0, 0, -1.405) for C165948 on the stock HRO footprint (table: (0, 0, -1.41)), (270, 0, 0) for
   the SOT-23-6 C2687116 and (270, 0, 3.74) for the ESP32-WROOM C701342. It assumes JLCPCB's zero
   rotation is the EasyEDA footprint as drawn and its origin the EasyEDA footprint origin; those three
   entries agree with that, but JLCPCB keeps its own per-part data, hence the preview check below.
5. End to end: on a snapshot of the routed board, every assembled part's JLCPCB footprint was placed from
   KiCad's position file through `cpl()` below and its pads compared with the board's: all 78 parts
   within 0.33 mm (the worst is the encoder pad noted below, the rest within 0.18 mm).

Bottom side (43 of the 78 assembled parts): the sister formula is right for the top only. KiCad's
position file gives a bottom part's rotation as stored on the board, unmirrored (KiCad 10, x not
negated), and a flipped footprint's local y axis is reversed. JLCPCB wants the bottom angle mirrored,
(180 - rotation + correction): that is what kicad-jlcpcb-tools (`fabrication.py`: "bottom angles need
to be mirrored on Y-axis", `(180 - rotation) % 360`, then the correction added) and KiBot
(`mirror_bottom`, noted as JLCPCB's behaviour) do, and the offset's dy changes sign. `cpl()` does both.
With the top formula used unchanged, every bottom part that is not at 0 / 180 with a 180-symmetric
footprint comes out wrong in the same end-to-end check (the ESP32 and the USB-C turned by 180, the SOTs
and the VSON too). This bottom rule has NOT been seen on a real order of ours (the sister board's
checked parts are on top), so it is the first thing to look at in the preview.

Look at these in JLCPCB's placement preview before paying:
- any bottom IC first (U6 ESP32, J1 USB-C, U1 / U3 / U5 / U7): if they are all mirrored or turned the
  same way, the bottom rule above is the cause, not the table;
- J1 (USB-C, the only offset, and on the bottom, so the dy sign rule matters): shell pegs in their slots;
- U4 (TPS61299, 6-ball WCSP, 0.4 mm pitch): ball A1 on the A1 land; a turn of 180 still looks plausible;
- U2 (BQ25173, VSON-10) and U6 (ESP32-C3-MINI-1): pin 1 corner;
- D1..D12 (WS2812B-2020): JLCPCB's footprint for C52917434 is named after the WS2815C-2020 (2.2 x 2.0),
  not the WS2812B-2020 of C965555; pad numbers and pin names agree, check the notch corner anyway;
- SW1 (encoder): the three-terminal side on A / C / B.
"""

import math

JLC_CORR = {
    # --- rotated in JLCPCB's library relative to KiCad's footprint
    "C7519": (270, 0, 0),      # USBLC6-2SC6, SOT-23-6 (JLC SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL). Residual 0.013 mm
    "C1852124": (270, 0, 0),   # TPS3840PL16DBVR, SOT-23-5 (JLC SOT-23-5_L2.9-W1.6-P0.95-LS2.8-BL). Residual 0.075 mm
    "C2066416": (270, 0, 0),   # TPS3840DL28DBVR, SOT-23-5 (same JLC footprint). Residual 0.075 mm
    "C2149796": (180, 0, 0),   # TPS22919DCKR, SOT-363 / SC-70-6 (JLC SC-70-6_L2.2-W1.3-P0.65-LS2.1-BR). Residual 0.113 mm
    # --- offset origin
    "C165948": (0, 0, -1.41),  # HRO TYPE-C-31-M-12 USB-C: JLC origin is the centre of its pad field, 1.41 mm
    #                            towards the pads from KiCad's. Fitted on the 4 shell pegs + 2 locating holes
    #                            (-1.405); residual 0.177 mm (signal pads, drawn 0.15 mm shorter by JLC)
    # --- polarised or asymmetric, fitted and found to need nothing (kept so that nobody wonders)
    "C41349510": (0, 0, 0),    # ESP32-C3-MINI-1-H4X (OCTAVE:ESP32-C3-MINI-1): all 53 pad names, residual 0.007 mm
    "C20345235": (0, 0, 0),    # BQ25173QWDRCRQ1, VSON-10 (Texas_DRC0010J_ThermalVias): residual 0.100 mm (pins 1-10
    #                            and the exposed pad; KiCad's four extra thermal-via lands have no JLC counterpart)
    "C20882919": (0, 0, 0),    # TPS61299YBHR, WCSP-6 (OCTAVE:TI_YBH0006_WCSP-6_0.8x1.2mm): residual 0.000 mm
    "C52917434": (0, 0, 0),    # WS2812B-2020 (JLC LED-SMD_4P-L2.2-W2.0-P1.00_WS2815C-2020-4P): residual 0.055 mm
    "C209762": (0, 0, 0),      # Alps EC11J1525402 (OCTAVE:RotaryEncoder_Alps_EC11J15_SMD): residual 0.315 mm, over
    #                            the 0.3 mm limit because of ONE pad: JLC draws its mounting lug 8 at x = 5.33 where
    #                            the other three are at 5.08 (ours: 5.00). Terminals within 0.04 mm, the other lugs
    #                            within 0.10 mm, bosses within 0.015 mm. JLC names: D, E = our S1, S2; 6..11 = MP
}

# Two-pad parts, all fitted at rotation 0 with no offset (0 and 180 are the same part), left out of the table:
#   0402 resistors C11702 C25092 C25104 C25741 C25744 C25755 C25764 C25905 C26083 C25779 C25790 C4109 (residual 0.080 mm)
#   0402 capacitors C15195 C1525 C1530 C52923 (0.060)   0603 capacitor C19702 (0.075)
#   0805 capacitors C1779 C45783 (0.050)   0603 fuse C99558 (0.037)   2520 inductor C98348 (0.125)


def cpl(x, y, rot, side, lcsc):
    """One row of KiCad's position file (PosX, PosY as exported: y up; Rot; Side "top" / "bottom") to
    JLCPCB's (Mid X, Mid Y, Rotation). For the top this is the sister project's formula unchanged."""
    drot, dx, dy = JLC_CORR.get(lcsc, (0, 0, 0))
    bottom = side == "bottom"
    if bottom:
        dy = -dy                      # a flipped footprint's local y runs the other way on the board
    r = math.radians(rot)
    x += dx * math.cos(r) + dy * math.sin(r)
    y -= -dx * math.sin(r) + dy * math.cos(r)
    return x, y, ((180 - rot if bottom else rot) + drot) % 360
