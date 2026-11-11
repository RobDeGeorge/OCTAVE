"""Sim 9: the TPS54360B buck (U7) switching, cycle by cycle (sim_buck.py is the averaged loop model).

TI's transient model is an encrypted PSpice model that ngspice cannot run, so this is a behavioural
peak-current-mode controller built from the datasheet (SNVSB93, TPS54360B): a clock from the RT resistor
(Eq. 7), frequency foldback, UVLO, the digital soft start, the gm error amplifier with its +/-30 uA output
and the board's compensation parts, COMP -> switch current at 12 A/V, a slope-compensation ramp, the
135 ns minimum on-time, the 5.5 A peak limit, the 0.6 V COMP clamp of Eco-mode (pulse skipping) and a
92 mOhm switch that may stay on over several clock periods (dropout). Round it is the board: the harness
and F1 / Q1 as the source, the input capacitors with their DC-bias loss and the 47 uF electrolytic, D5
(B560C), L1 (8.2 uH, 31 mOhm), 3 x 47 uF 10 V on the output, the FB divider (68k / 12k = 5.33 V) and,
for the cranking case, the SS34 OR diode and the 3.3 V LDO's load behind it.

It is not the vendor model. What it cannot tell: anything that depends on the layout (switch-node
ringing, the spikes that ESL puts on the ripple, EMI), losses and heat, the real slope compensation (TI
does not publish it; `SE` below is an assumption and part (e) sweeps it), the bootstrap refresh in
dropout (the datasheet's 0.99 duty factor is applied by hand) and inductor saturation (L is linear; the
peaks are compared with the rating).

Assumed, not from a datasheet: SE; the foldback thresholds (FB 0.6 / 0.4 / 0.2 V); ISKIP (peak current at
COMP = 0.6 V, from the datasheet's "enters Eco-mode at about 24 mA"); the upper COMP clamp; ceramic
capacitance under DC bias (COUT_EACH, cin()); the electrolytic's ESR; the harness (50 mOhm, 2 uH).
L1's ratings (31 mOhm max, 4.2 A heating, 7 A saturation) are a distributor's listing of the Sunlord
SWPA8065S8R2MT, the AP2114H's dropout (0.45 V typ, 0.75 V max at 1 A) a datasheet mirror.

Run with python3 -I (about twelve minutes, one ngspice at a time), or name the parts to run again:
startup steady step crank dropout subharmonic. Results: out/buck_sw.json.
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from gen import OUT, run

# ---- the board (gen/obd_adapter.py, power block) ----
R_RT = 160e3                      # BUCK_RT
R_TOP, R_BOT = 68e3, 12e3         # FB divider
R_COMP, C_COMP, C_POLE = 13e3, 6.8e-9, 47e-12
L1, L1_DCR, L1_ISAT, L1_IRMS = 8.2e-6, 0.031, 7.0, 4.2
COUT_EACH = 17e-6                 # 47 uF 10 V X5R 1206 at 5.3 V: 15-20 uF (the design's own estimate)
COUT_N, COUT_ESR = 3, 3e-3        # three in parallel, 3 mOhm each
ELEC, ELEC_ESR = 47e-6, 1.0       # 47 uF 50 V 6.3 x 7.7 aluminium, general purpose (66 mA ripple at 120 Hz)
# ---- TPS54360B (SNVSB93) ----
FSW = 92417 * (R_RT / 1e3) ** -0.991 * 1e3      # Eq. 7
T = 1 / FSW
VREF, GM_EA, GM_SS, AOL, UGBW, GMPS = 0.8, 350e-6, 77e-6, 10000, 2.5e6, 12.0
RDSON, ILIM, TON_MIN = 0.092, 5.5, 135e-9
UVLO_ON, UVLO_OFF = 4.3, 4.3 - 0.325
TSS = 1024 / FSW / 0.8            # Eq. 6 is the 10-90 % time; this is 0-100 %
SE = 0.6e6                        # slope compensation, A/s referred to the switch current: ASSUMED
ISKIP = 0.17                      # peak switch current at COMP = 0.6 V: ASSUMED (see the docstring)
VOUT = VREF * (1 + R_TOP / R_BOT)
VF = 0.40                         # B560C at about 1 A, for the hand formulas only


def cin(vin):
    """Effective input ceramics at this bias: 10 uF 50 V 1206 and each 2.2 uF 50 V 1206 (typical X5R / X7R
    curves, ASSUMED)."""
    pts10 = [(0, 10e-6), (6, 8e-6), (12, 6e-6), (24, 3.5e-6), (42, 2e-6), (60, 1.5e-6)]
    pts22 = [(0, 2.2e-6), (6, 2.1e-6), (12, 1.9e-6), (24, 1.5e-6), (42, 1.0e-6), (60, 0.8e-6)]
    x = [p[0] for p in pts10]
    return float(np.interp(vin, x, [p[1] for p in pts10])), float(np.interp(vin, x, [p[1] for p in pts22]))


def deck(vbat, iload, tstop, tstart=0.0, ic=None, se=SE, cout=COUT_EACH * COUT_N, ind=L1, tmax=25e-9,
         tprint=20e-9, vin_caps=12.0, second=None, rds=RDSON):
    """vbat / iload: source strings for the battery and for the load on 5V_BUCK. ic: None = everything at
    zero (start-up), else dict(vin, iload) to start in regulation. second: load in A behind the SS34 OR
    diode (the LDO passes its load current on to 5V)."""
    c10, c22 = cin(vin_caps)
    ro, co = AOL / GM_EA, GM_EA / (2 * math.pi * UGBW)
    vch = 0.6 + (ILIM - ISKIP + se * T) / GMPS
    a = []
    A = a.append
    A(f"* fsw={FSW:.0f} T={T:.4g} vout={VOUT:.3f} se={se:.3g} cout={cout:.3g} l={ind:.3g} c10={c10:.3g} c22={c22:.3g}")
    A(".func sg(x) {0.5 * (1 + tanh(x))}")
    # --- source: battery, harness, F1 + the ideal diode (11 mV regulated drop, 10 mOhm; blocks reverse) ---
    A(vbat)
    A("Rharn bat h1 0.05\nLharn h1 h2 2u\nRlh h1 h2 20\nRF1 h2 vbat_f 0.1")
    A("Bq1 vbat_f vbat_p I = max(0, V(vbat_f,vbat_p) - 0.011) / 0.010")
    # --- input capacitors on VBAT_P ---
    v0 = ic["vin"] if ic else 0.0
    A(f"C10 vbat_p c10e {c10} ic={v0}\nR10e c10e 0 4m")
    A(f"C22a vbat_p c22ae {2 * c22} ic={v0}\nR22e c22ae 0 3m")
    A(f"C100n vbat_p 0 100n ic={v0}")
    A(f"Celec vbat_p cele {ELEC} ic={v0}\nRele cele 0 {ELEC_ESR}")
    # --- power stage ---
    # the switch follows q through a sharp step at 0.5, so that q > 0.5 is "on" in the measurements
    A(f"Bsw vbat_p swx I = V(vbat_p,swx) / {rds} * sg((V(q) - 0.5) * 20) + V(vbat_p,swx) * 1e-7")
    A("Vsns swx sw 0")
    A("Dhs sw vbat_p DHSB\n.model DHSB D(Is=1e-10 N=1.2 Rs=0.1 Bv=70)")
    A("D5 0 sw B560C")
    il0 = ic["iload"] + (second or 0.0) if ic else 0.0
    A(f"L1 sw lx {ind} ic={il0}\nVil lx ly 0\nRdcr ly vout {L1_DCR}")
    vo0 = VOUT if ic else 0.0
    A(f"Cout vout coe {cout} ic={vo0}\nRcoe coe 0 {COUT_ESR / COUT_N}")
    A(f"Rtop vout fb {R_TOP}\nRbot fb 0 {R_BOT}\nCfb fb 0 3p")
    A(iload)
    if second is not None:
        # 5V_BUCK -> SS34 -> 5V (2 x 10 uF, about 6 uF each at 5 V) -> the LDO as its load current
        A(f"D6 vout v5 SS34\nC5 v5 0 12u ic={max(vo0 - 0.35, 0)}\nI3v3 v5 0 {second}")
    # --- UVLO (a latch: set above 4.3 V, cleared below 3.975 V) ---
    en0 = 1 if ic else 0
    A(f"Ben 0 en I = 1m * (sg((V(vbat_p) - {UVLO_ON}) * 50) * (1 - V(en)) - sg(({UVLO_OFF} - V(vbat_p)) * 50) * V(en))")
    A(f"Cen en 0 10p ic={en0}\nRen en 0 1e12")
    # --- soft start: the reference ramps to 0.8 V in TSS ---
    css = 100e-9
    A(f"Bss 0 ss I = sg((V(en) - 0.5) * 20) * {css * VREF / TSS} * sg((0.9 - V(ss)) * 100)"
      " - (1 - sg((V(en) - 0.5) * 20)) * 10m * V(ss)")
    A(f"Css ss 0 {css} ic={1.0 if ic else 0}\nRss ss 0 1e12")
    A(f"Bref ref 0 V = min(V(ss), {VREF})")
    # --- error amplifier and COMP ---
    A(f"Bea 0 comp I = sg((V(en) - 0.5) * 20) * max(-30u, min(30u, ({GM_SS} + {GM_EA - GM_SS}"
      f" * sg((V(ss) - {VREF}) * 200)) * (V(ref) - V(fb))))")
    A(f"Ro comp 0 {ro}\nCo comp 0 {co}")
    A(f"Bclamp comp 0 I = 10m * max(0, V(comp) - {vch}) - 10m * max(0, 0.58 - V(comp))")
    vc0 = 0.6 + (il0 + 0.35 - ISKIP + se * T * 0.5) / GMPS if ic else 0.58
    A(f"Rcomp comp comp_rc {R_COMP}\nCcomp comp_rc 0 {C_COMP} ic={vc0}\nCpole comp 0 {C_POLE} ic={vc0}")
    # --- oscillator, foldback (clock / 1, 2, 4, 8 as FB falls), slope ramp ---
    for n in (1, 2, 4, 8):
        A(f"Vclk{n} clk{n} 0 PULSE(0 1 20n 5n 5n 30n {n * T})")
    A("Bclk clk 0 V = V(clk8) + sg((V(fb) - 0.2) * 500) * (V(clk4) - V(clk8))"
      " + sg((V(fb) - 0.4) * 500) * (V(clk2) - V(clk4)) + sg((V(fb) - 0.6) * 500) * (V(clk1) - V(clk2))")
    A(f"Vsaw saw 0 PULSE(0 1 20n {T - 6e-9} 3n 1n {T})")
    # --- PWM: latch q set by the clock (not in UVLO, not while COMP is on its 0.6 V clamp), cleared when the
    #     switch current reaches the COMP level less the ramp, or the peak limit, after the minimum on-time ---
    A(f"Bith ith 0 V = {GMPS} * (V(comp) - 0.6) + {ISKIP} - {se * T} * V(saw)")
    A("Bset set0 0 V = sg((V(clk) - 0.5) * 20) * sg((V(en) - 0.5) * 20) * sg((V(comp) - 0.6) * 2000)")
    A("Rset set0 setn 1k\nCset setn 0 2p ic=0")
    A(f"Brst rst0 0 V = max(sg((V(qd) - 0.5) * 40) * max(sg((I(Vsns) - V(ith)) * 200), sg((I(Vsns) - {ILIM}) * 200)),"
      " 1 - sg((V(en) - 0.5) * 20))")
    A("Rrst rst0 rst 1k\nCrst rst 0 2p ic=0")
    # reset wins over set; with neither, q is pulled to the rail it is nearer (so it holds)
    A("Bq 0 q I = 1m * (V(rst) * (0 - V(q)) + (1 - V(rst)) * (V(setn) * (1 - V(q))"
      " + (1 - V(setn)) * 0.3 * (sg((V(q) - 0.5) * 20) - V(q))))")
    A("Cq q 0 4p ic=0\nRq q 0 1e12")
    # qd: q delayed by the minimum on-time (RC to 0.5 = 0.693 RC). It is cleared only once q is well below the
    # switch's threshold: with one threshold for both, q = 0.6 with qd and rst half way is a stable state and
    # the switch stays on (seen at start-up, where the first pulse is cut at the minimum on-time)
    A(f"Bqd 0 qd I = sg((V(q) - 0.5) * 20) * (1 - V(qd)) / {TON_MIN / 0.693 / 1e-9:.1f}"
      " - sg((0.2 - V(q)) * 40) * V(qd) / 3")
    A("Cqd qd 0 1n ic=0\nRqd qd 0 1e12")
    ics = []
    if ic:
        ics = [f"v(vbat_p)={v0}", f"v(vbat_f)={v0}", f"v(vout)={VOUT}", "v(coe)=0", f"v(comp)={vc0}",
               f"v(comp_rc)={vc0}", "v(ss)=1.0", "v(en)=1", f"v(fb)={VREF}", "v(q)=0", "v(qd)=0"]
        if second is not None:
            ics.append(f"v(v5)={VOUT - 0.35}")
        A(".ic " + " ".join(ics))
    A(".options reltol=1e-3 abstol=1e-9 vntol=1e-6 method=gear itl4=50")
    A(f".tran {tprint} {tstop} {tstart} {tmax} uic")
    return "\n".join(a)


SAVE = "v(vout) i(Vil) v(q) v(comp) v(vbat_p) v(cele) i(Vbat) v(fb)"
COLS = dict(vout=1, il=3, q=5, comp=7, vin=9, vele=11, ibat=13, fb=15)


def tran(tag, body, extra="", timeout=1500):
    f = os.path.join(OUT, tag + ".dat")
    save = SAVE + (" " + extra if extra else "")
    body += f"\n.save {save}\n.control\nrun\nlinearize\nwrdata {f} {save}\n.endc"
    meas, log = run(tag, body, timeout=timeout)
    if not os.path.exists(f):
        raise RuntimeError(log[-2000:])
    d = np.loadtxt(f)
    os.remove(f)                  # tens of MB each; the numbers go to buck_sw.json
    w = {k: d[:, c] for k, c in COLS.items()}
    w["t"] = d[:, 0]
    for i, name in enumerate(extra.split()):
        w[name] = d[:, 17 + 2 * i]
    w["ibat"] = -w["ibat"]
    return w


def edges(t, q):
    """Times of q's rising and falling edges (0.5 crossings)."""
    hi = q > 0.5
    up = np.nonzero(~hi[:-1] & hi[1:])[0]
    dn = np.nonzero(hi[:-1] & ~hi[1:])[0]
    return t[up], t[dn]


def steady(w, vin, iout):
    """Figures of one settled window."""
    t, q, il, vo = w["t"], w["q"], w["il"], w["vout"]
    span = t[-1] - t[0]
    up, dn = edges(t, q)
    n_clk = span / T
    r = dict(vin=vin, iout=iout, vout=round(float(vo.mean()), 4), ripple_mV=round(float(np.ptp(vo)) * 1e3, 2),
             il_pk=round(float(il.max()), 3), il_min=round(float(il.min()), 3),
             duty=round(float((q > 0.5).mean()), 4), pulses_per_clock=round(len(up) / n_clk, 3),
             vin_ripple_mV=round(float(np.ptp(w["vin"])) * 1e3, 1),
             elec_rms_mA=round(float(np.sqrt(np.mean((w["vele"] / ELEC_ESR) ** 2))) * 1e3, 1),
             comp=round(float(w["comp"].mean()), 3), vbat_p=round(float(w["vin"].mean()), 3))
    # on-times of whole pulses, and how much consecutive ones differ (period doubling shows as alternation)
    if len(up) > 4 and len(dn) > 4:
        dn2 = dn[dn > up[0]]
        n = min(len(up), len(dn2))
        ton = dn2[:n] - up[:n]
        r["ton_ns"] = round(float(ton.mean()) * 1e9, 1)
        r["ton_alt"] = round(float(np.abs(np.diff(ton)).mean() / ton.mean()), 3)
    ppc = r["pulses_per_clock"]
    if ppc > 0.97:
        r["mode"] = "CCM" if r["il_min"] > 0.01 else "DCM"
        if r.get("ton_alt", 0) > 0.1:
            r["mode"] += ", on-time alternates"
    elif r["il_min"] > 0.01:
        r["mode"] = "CCM, on over several periods" if r["duty"] > 0.8 else "CCM, periods missed"
    else:
        r["mode"] = "pulse skipping"
    return r


def hand(vin, iout, ind=L1):
    """Text-book CCM figures to check the model against."""
    d = (VOUT + VF + iout * L1_DCR) / (vin - iout * RDSON + VF)
    di = (vin - iout * RDSON - VOUT - iout * L1_DCR) * d / (ind * FSW)
    cout = COUT_EACH * COUT_N
    dv = math.hypot(di / (8 * FSW * cout), di * COUT_ESR / COUT_N)   # the two are in quadrature
    return dict(duty=round(d, 4), dI=round(di, 3), ripple_mV=round(dv * 1e3, 2), ccm=iout > di / 2)


def ss_case(vin, iout, se=SE, window=0.6e-3, settle=3.0e-3, tag=None, **kw):
    tag = tag or f"bsw_ss_{vin}_{iout}"
    body = deck(f"Vbat bat 0 {vin}", f"Iload vout 0 {iout}", settle + window, tstart=settle,
                ic=dict(vin=vin, iload=iout), se=se, vin_caps=vin, **kw)
    return steady(tran(tag, body), vin, iout)


if __name__ == "__main__":
    only = sys.argv[1:]            # parts to run: startup steady step crank dropout subharmonic (none = all)
    path = os.path.join(OUT, "buck_sw.json")
    res = json.load(open(path)) if only and os.path.exists(path) else {}
    res["model"] = dict(fsw_kHz=round(FSW / 1e3, 1), vout=round(VOUT, 3), tss_ms=round(TSS * 1e3, 2),
                        se_A_per_us=SE / 1e6, iskip=ISKIP, cout_uF=COUT_EACH * COUT_N * 1e6,
                        l_uH=L1 * 1e6, dcr=L1_DCR, rdson=RDSON)
    print(res["model"])

    def want(k):
        return not only or k in only

    def save():
        json.dump(res, open(path, "w"), indent=1)

    # (a) start-up at 12 V into 0.3 A: battery applied in 100 us through the harness
    if want("startup"):
        res["startup"] = []
        for iout, name in ((0.3, "0.3A"), (1.0, "1.0A")):
            w = tran(f"bsw_start_{name}", deck("Vbat bat 0 PWL(0 0 100u 12)",
                                               f"Bload vout 0 I = {iout} * min(1, V(vout) / 4.5)", 5e-3, vin_caps=12))
            t, vo = w["t"], w["vout"]
            end = vo[t > 4.5e-3].mean()
            t10 = t[np.nonzero(vo > 0.1 * end)[0][0]]
            t90 = t[np.nonzero(vo > 0.9 * end)[0][0]]
            sw = t > 0.3e-3
            r = dict(iout=iout, vout_end=round(float(end), 4), overshoot_mV=round(float(vo.max() - end) * 1e3, 1),
                     rise_10_90_ms=round(float(t90 - t10) * 1e3, 3), il_pk=round(float(w["il"].max()), 3),
                     ibat_pk_plug=round(float(w["ibat"][~sw].max()), 2),
                     ibat_pk_softstart=round(float(w["ibat"][sw].max()), 3),
                     vbat_p_pk=round(float(w["vin"].max()), 2))
            res["startup"].append(r)
            print("startup", r)

    save()

    # (b) steady state
    if want("steady"):
        res["steady"] = []
        for vin in (9, 12, 14.4, 24, 42):
            for iout in (0.05, 0.3, 0.6, 1.5):
                r = ss_case(vin, iout, window=1.0e-3 if iout < 0.1 else 0.6e-3)
                r["hand"] = hand(vin, iout)
                res["steady"].append(r)
                print("steady", r)

    save()

    # (c) load step 0.1 -> 1.0 A and back (1 us edges), at 12 V; the low and the nominal output capacitance
    if want("step"):
        res["step"] = []
        import sim_buck
        for cout in (COUT_EACH * COUT_N, 12e-6 * COUT_N):
            load = "Iload vout 0 PWL(0 0.1 2m 0.1 2.001m 1.0 3.5m 1.0 3.501m 0.1)"
            w = tran(f"bsw_step_{cout * 1e6:.0f}u", deck("Vbat bat 0 12", load, 5.5e-3, ic=dict(vin=12, iload=0.1),
                                                         cout=cout, vin_caps=12))
            t, vo = w["t"], w["vout"]
            pre = vo[(t > 1.6e-3) & (t < 2e-3)].mean()
            hi = vo[(t > 3.2e-3) & (t < 3.5e-3)].mean()
            k1 = (t > 2e-3) & (t < 3.5e-3)
            k2 = t > 3.5e-3
            # settling: last time the cycle-averaged output is more than 10 mV from its final value
            n = max(1, int(round(T / (t[1] - t[0]))))
            avg = np.convolve(vo, np.ones(n) / n, mode="same")

            def settle(k, final, t0, t=t, avg=avg):
                bad = np.nonzero(k & (np.abs(avg - final) > 0.010))[0]
                return round(float(t[bad[-1]] - t0) * 1e6, 0) if len(bad) else 0.0
            r = dict(cout_uF=cout * 1e6, v_before=round(float(pre), 4), v_loaded=round(float(hi), 4),
                     undershoot_mV=round(float(pre - vo[k1].min()) * 1e3, 1),
                     overshoot_mV=round(float(vo[k2].max() - pre) * 1e3, 1),
                     settle_load_us=settle(k1 & (t < 3.45e-3), hi, 2e-3),
                     settle_unload_us=settle(k2 & (t < 5.4e-3), vo[t > 5.3e-3].mean(), 3.5e-3),
                     il_pk=round(float(w["il"].max()), 3))
            # the averaged model (sim_buck.py) with the board's divider and the same step
            mc = 1 + SE / ((12 - VOUT) / L1)
            tag = f"bsw_avg_{cout * 1e6:.0f}u"
            d = sim_buck.deck(12, cout, COUT_ESR / COUT_N, 0.1, mc, "tran", tag, istep=1.0)
            run(tag, d)
            f = os.path.join(OUT, tag + ".dat")
            dd = np.loadtxt(f)
            os.remove(f)
            ta, va = dd[:, 0], dd[:, 1]
            pa = va[(ta > 3.5e-3) & (ta < 4e-3)].mean()
            r["avg_model"] = dict(mc=round(mc, 2),
                                  undershoot_mV=round(float(pa - va[(ta > 4e-3) & (ta < 6e-3)].min()) * 1e3, 1),
                                  overshoot_mV=round(float(va[ta > 6e-3].max() - pa) * 1e3, 1))
            res["step"].append(r)
            print("step", r)

    save()

    # (d) cold crank, compressed in time (the loop settles in well under a millisecond): 12 V -> 6 V in 2 ms,
    #     6 V for 6 ms, 7 V for 4 ms, back to 12 V. 0.1 A on 5V_BUCK, the 3.3 V load behind the OR diode.
    if want("crank"):
        res["crank"] = []
        for i33, rds in ((0.25, RDSON), (0.5, RDSON), (0.5, 0.19)):
            vb = "Vbat bat 0 PWL(0 12 1m 12 3m 6 9m 6 9.5m 7 13.5m 7 15m 12 17m 12)"
            w = tran(f"bsw_crank_{i33}_{rds}", deck(vb, "Iload vout 0 0.1", 17e-3, ic=dict(vin=12, iload=0.1),
                                                    second=i33, tmax=6e-9, tprint=40e-9, rds=rds), extra="v(v5)",
                     timeout=3000)
            t, vo, v5 = w["t"], w["vout"], w["v(v5)"]
            k6 = (t > 6e-3) & (t < 9e-3)
            k7 = (t > 11e-3) & (t < 13.5e-3)
            drop = 0.75 * i33                 # AP2114H dropout, worst case, scaled from 0.75 V at 1 A
            r = dict(i_3v3=i33, rdson=rds, vbat_p_at_6V=round(float(w["vin"][k6].mean()), 3),
                     vout_min=round(float(vo.min()), 3), vout_at_6V=round(float(vo[k6].mean()), 3),
                     vout_at_7V=round(float(vo[k7].mean()), 3), ripple_at_6V_mV=round(float(np.ptp(vo[k6])) * 1e3, 1),
                     v5_min=round(float(v5.min()), 3), ldo_headroom_min=round(float(v5.min()) - 3.3, 3),
                     ldo_needs=round(drop, 3), ldo_regulates=bool(v5.min() - 3.3 > drop),
                     duty_at_6V=round(float((w["q"][k6] > 0.5).mean()), 3), il_pk=round(float(w["il"].max()), 3),
                     vout_max=round(float(vo.max()), 3))
            res["crank"].append(r)
            print("crank", r)

    save()

    # how low the battery may go (0.6 A in all): steady state near and in dropout
    if want("dropout"):
        res["dropout"] = []
        for vin in (7.0, 6.5, 6.0, 5.8, 5.6, 5.4, 5.0, 4.5):
            r = ss_case(vin, 0.6, tag=f"bsw_drop_{vin}", settle=4e-3)
            # datasheet Eq. 1 turned round: the output that a 99 % duty limit leaves (the boot capacitor's
            # refresh, which the model does not have), from VBAT_P as simulated
            r["eq1_vout_max"] = round(0.99 * (r["vbat_p"] - 0.6 * RDSON + VF) - VF - 0.6 * L1_DCR, 3)
            r["regulates"] = bool(r["vout"] > VOUT - 0.02 and r["eq1_vout_max"] > VOUT)
            res["dropout"].append(r)
            print("dropout", r)

    save()

    # (e) subharmonic check: high duty, with less and more slope compensation than assumed
    if want("subharmonic"):
        res["subharmonic"] = []
        for vin in (6.5, 7.5, 9.0, 12.0):
            for se in (0.0, 0.15e6, 0.3e6, 0.6e6, 1.2e6):
                r = ss_case(vin, 0.6, se=se, tag=f"bsw_sub_{vin}_{se / 1e6}", settle=3e-3)
                m1 = (vin - 0.6 * RDSON - VOUT - 0.6 * L1_DCR) / L1
                m2 = (VOUT + VF + 0.6 * L1_DCR) / L1
                r = dict(vin=vin, se_A_per_us=se / 1e6, se_needed_A_per_us=round(max(0, (m2 - m1) / 2) / 1e6, 3),
                         duty=r["duty"], ton_alt=r.get("ton_alt"), pulses_per_clock=r["pulses_per_clock"],
                         ripple_mV=r["ripple_mV"], il_pk=r["il_pk"], mode=r["mode"])
                res["subharmonic"].append(r)
                print("subharmonic", r)
        # inductor at -30 % (tolerance and bias) and the low output capacitance, 9 V and 14.4 V, 1.5 A
        res["corners"] = []
        for vin in (9, 14.4, 42):
            r = ss_case(vin, 1.5, tag=f"bsw_corner_{vin}", ind=0.7 * L1, cout=12e-6 * COUT_N)
            res["corners"].append(r)
            print("corner", r)

    save()
