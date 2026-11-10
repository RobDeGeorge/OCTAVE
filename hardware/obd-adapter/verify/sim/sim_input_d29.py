"""Sim 8: the input path with a B560C Schottky ("D29") in place of the LM74700 + AO4264E ideal diode.

The Schottky was tried on 2026-11-09 to save three parts and taken out again because of what this deck
shows (REVIEW 54): reverse leakage takes VBAT_P to -0.7 V at 100 C with the battery reversed, and the
drop costs 0.3 V of cranking headroom. The board has the ideal diode (`fet` below).

A / B in one deck: `fet` is the behavioural ideal diode of sim_input.py, `d29` the Schottky. Everything
else is the board of 2026-11-09: F1 as its resistance (the events here are too short to heat
it), the TVS, 10u + 100n + 2 x 2.2u + 47u (electrolytic, with ESR) on VBAT_P, no EN divider (the buck runs
down to its 4.3 V UVLO), the buck as a constant-power load.

With the battery reversed nothing on VBAT_P draws current, so the node goes where the diode's leakage
takes it: negative, until the buck's own diodes conduct (GND -> catch diode D5 -> SW -> the high-side
FET's body diode -> VIN). `hot` is the B560C's 100 C leakage limit (20 mA at 60 V), `cold` its 25 C limit
(0.5 mA at 60 V), both as resistors, which overstates the leakage at -12 V.

Run with python3 -I. Results: out/input_d29.json.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from gen import OUT, run

TVS = ('Vtvs vbat_f d4top 0\nD4a d4top d4m DTVS\nD4b 0 d4m DTVS\n'
       '.model DTVS D(Is=1e-12 N=1 Rs=0.0715 Bv={bv} Ibv=1m Cjo=2n)')
FET = """
Bgate g_cmd 0 V = 0.25*(1+tanh((V(vbat_f)-V(vbat_p)+0.011)/2m))*(1+tanh((V(vbat_f)-3.2)/0.1))
Rg g_cmd g_f 500
Cg g_f 0 1n
BQ1 vbat_f vbat_p I = V(vbat_f,vbat_p)/0.010 * max(0,min(1,V(g_f)))
DQ1b vbat_f vbat_p DBODY4264
.model DBODY4264 D(Is=1e-9 N=1.3 Rs=0.01 Cjo=500p Bv=65)
"""
# B560C: VF 0.70 V max at 5 A (about 0.30 V at 0.25 A, 0.37 V at 1 A typ), 60 V, Cj about 300 pF
D29 = """
D29 vbat_f vbat_p DB560C
.model DB560C D(Is=5e-6 N=1.05 Rs=0.04 Bv=66 Ibv=1m Cjo=500p)
Rleak vbat_f vbat_p {rleak}
"""
REST = """
RF1 vbat_in vbat_f 0.1
C21 vbat_p 0 10u
C23 vbat_p 0 100n
C24 vbat_p 0 2.2u
C25 vbat_p 0 2.2u
C22 vbat_p c22e 47u
Resr c22e 0 {esr}
* the buck: 150 uA when idle, constant power above its 4.3 V UVLO, and its diodes for a negative input
Bbuck vbat_p 0 I = 0.5*(1+tanh(V(vbat_p)/0.05))*(150u + 0.5*(1+tanh((V(vbat_p)-4.3)/0.1))*{watts}/max(V(vbat_p),4.3))
D5 0 sw DB560C5
.model DB560C5 D(Is=5e-6 N=1.05 Rs=0.04 Bv=66 Cjo=500p)
Dhs sw vbat_p DBODYHS
.model DBODYHS D(Is=1e-10 N=1.2 Rs=0.1 Bv=70)
"""


def tran(tag, src, tstop, tstep, kind, watts=3.5, bv=28.25, esr=0.5, rleak="120k"):
    f = os.path.join(OUT, tag + ".dat")
    body = "\n".join([src, TVS.format(bv=bv), FET if kind == "fet" else D29.format(rleak=rleak),
                      REST.format(esr=esr, watts=watts), ".options reltol=1e-4 method=gear itl4=100",
                      f".tran {tstep} {tstop} 0 {tstep}",
                      ".control\nrun\nwrdata " + f + " v(vbat_f) v(vbat_p) i(Vs)\n.endc"])
    run(tag, body)
    d = np.loadtxt(f)
    return d[:, 0], d[:, 1], d[:, 3], d[:, 5]


res = {}
# (a) forward drop at three loads (W at 5 V / 0.85)
for amps in (0.1, 0.25, 0.5):
    t, vf, vp, i = tran(f"d29_drop_{amps}", "Vs src 0 12.6\nRs src vbat_in 0.01", "20m", "20u", "d29",
                        watts=amps * 12.6)
    res[f"drop_{amps}A"] = round(float((vf - vp)[-1]), 3)
# (b) reversed battery
for vr in (-12, -16, -24):
    for name, rleak in (("cold", "120k"), ("hot", "3k")):
        t, vf, vp, i = tran(f"d29_rev{vr}_{name}", f"Vs src 0 PWL(0 0 1m {vr})\nRs src vbat_in 0.01", "40m", "20u",
                            "d29", rleak=rleak)
        res[f"reverse_{vr}V_{name}"] = dict(vbat_p=round(float(vp[-1]), 3), i_mA=round(float(-i[-1] * 1e3), 3))
    t, vf, vp, i = tran(f"fet_rev{vr}", f"Vs src 0 PWL(0 0 1m {vr})\nRs src vbat_in 0.01", "40m", "20u", "fet")
    res[f"reverse_{vr}V_fet"] = dict(vbat_p=round(float(vp[-1]), 3), i_mA=round(float(-i[-1] * 1e3), 3))
# (c) ISO 7637-2 pulse 1 (-150 V, Ri 10 ohm, 2 ms) at a light and a heavy load, room and cold electrolytic
P1 = "Vs src 0 PWL(0 13.5 100u 13.5 101u -136.5 2.1m 13.5 3m 13.5)\nRs src vbat_in 10"
for watts in (1.0, 3.5):
    for esr in (0.5, 5.0):
        for kind in ("fet", "d29"):
            t, vf, vp, i = tran(f"{kind}_p1_{watts}_{esr}", P1, "3m", "100n", kind, watts=watts, esr=esr)
            res[f"pulse1_{watts}W_esr{esr}_{kind}"] = dict(
                vbat_p_min=round(float(vp[t > 5e-6].min()), 2), reverse_max=round(float((vp - vf).max()), 1),
                below_4V3_ms=round(float(((vp < 4.3) & (t > 5e-6)).sum() * 100e-9 * 1e3), 2))
# (d) suppressed load dump 5b (35 V, 400 ms, Ri 0.5) and pulse 2a (+112 V, Ri 2, 50 us): VBAT_P peak
LD = "Vs src 0 PWL(0 13.5 50m 13.5 55m 35 455m 35 505m 13.5 900m 13.5)\nRs src vbat_in 0.5"
P2A = ("Vb bsrc 0 13.5\nLlisn bsrc bl 5u\nRlisn bl vbat_in 0.01\n"
       "Vs src 0 PWL(0 0 100u 0 101u 112 150u 0 400u 0)\nRs src vbat_in 2")
for kind in ("fet", "d29"):
    t, vf, vp, i = tran(f"{kind}_5b", LD, "0.9", "50u", kind, bv=31.25)
    res[f"loaddump_{kind}"] = dict(vbat_p_max=round(float(vp.max()), 2))
    t, vf, vp, i = tran(f"{kind}_2a", P2A, "400u", "20n", kind, bv=31.25)
    res[f"pulse2a_{kind}"] = dict(vbat_p_max=round(float(vp.max()), 2))
# (e) cold crank: 12 V -> 6 V for 100 ms -> 7 V for 1 s, heavy load
CR = "Vs src 0 PWL(0 12 50m 12 52m 6 152m 6 160m 7 1.16 7 1.2 12 1.5 12)\nRs src vbat_in 0.02"
for kind in ("fet", "d29"):
    t, vf, vp, i = tran(f"{kind}_crank", CR, "1.5", "100u", kind)
    res[f"crank_{kind}"] = dict(vbat_p_min=round(float(vp[t > 10e-3].min()), 2))
for k, v in res.items():
    print(k, v)
json.dump(res, open(os.path.join(OUT, "input_d29.json"), "w"), indent=1)
