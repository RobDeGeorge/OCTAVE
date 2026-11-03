"""Sim 5 + 7: input path (PTC F1, TVS D4, LM74700 U5 + AO4264E Q1), VBAT_P stress, buck EN UVLO, VBAT_SENSE."""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from gen import run, OUT

# SMDJ26CA (D4, C521619): VRWM 26 V, VBR 28.9 min / 30.4 typ / 31.9 max @ 1 mA, VC 42.1 V @ IPP 71.4 A.
def tvs(vbr):
    bv = vbr - 0.65
    return (f'Vtvs vbat_f d4top 0\nD4a d4top d4m DTVS\nD4b 0 d4m DTVS\n'
            f'.model DTVS D(Is=1e-12 N=1 Rs=0.0715 Bv={bv} Ibv=1m Cjo=2n)   ; D4 SMDJ26CA bidirectional')

PATH = r"""
* ---- F1 polyfuse 1.1 A hold / 2.2 A trip, 60 V (Littelfuse 2920L110/60): thermal behavioural model
*   R0 0.15 ohm, Rth 250 K/W, Cth 0.029 J/K: holds 1.1 A, trips ~1.6-2 A, ~0.3 s at 8 A
BF1 vbat_in vbat_f I = V(vbat_in,vbat_f)/(0.15*(1+1e4/(1+exp(-(V(tf1)-100)/3))))
BPF1 0 tf1 I = V(vbat_in,vbat_f)*V(vbat_in,vbat_f)/(0.15*(1+1e4/(1+exp(-(V(tf1)-100)/3))))
RthF1 tf1 0 250
CthF1 tf1 0 0.029
* ---- U5 LM74700 + Q1 AO4264E (60 V, 9.8 mohm): behavioural ideal diode.
*   on when ANODE (vbat_f) > 3.2 V and above CATHODE: Rds 10 mohm; reverse comparator turns it off in ~0.5 us
*   (DS: 0.45 us typ reverse turn-off); body diode always present.
Bgate g_cmd 0 V = 0.25*(1+tanh((V(vbat_f)-V(vbat_p)+0.011)/2m))*(1+tanh((V(vbat_f)-3.2)/0.1))
Rg g_cmd g_f 500
Cg g_f 0 1n
BQ1 vbat_f vbat_p I = V(vbat_f,vbat_p)/0.010 * max(0,min(1,V(g_f)))
DQ1b vbat_f vbat_p DBODY4264
.model DBODY4264 D(Is=1e-9 N=1.3 Rs=0.01 Cjo=500p Bv=65)
RU5leak vbat_f 0 1Meg            ; LM74700 + EN pin + C16 leakage (order 1-10 uA)
* ---- VBAT_P parts
C17 vbat_p 0 10u
C18 vbat_p 0 100n
C19 vbat_p 0 2.2u
C20 vbat_p 0 2.2u
R13 vbat_p buck_en 510k
R14 buck_en 0 84.5k
* TPS54360B EN pin: I1 1.2 uA always, Ihys 3.4 uA once EN > 1.2 V (state latched by a hysteretic switch)
IEN1 0 buck_en 1.2u
SEN buck_en_ref 0 buck_en 0 SWEN
.model SWEN SW(Ron=1 Roff=1e9 Vt=1.2 Vh=0.001)
Ren_st buck_en_ref vonst 1k
Von vonst 0 1
Bhys 0 buck_en I = 3.4u*0.5*(1+tanh((-I(Von)-0.5m)/0.1m))
* buck as a constant-power load while enabled (5 V x 0.6 A / 0.85), resistive below 4 V input
Bbuck vbat_p 0 I = 150u + 0.5*(1+tanh((-I(Von)-0.5m)/0.1m))*0.5*(1+tanh((V(vbat_p)-4.3)/0.1))*3.5/max(V(vbat_p),4.3)
* DLC_SW front ends on (~15 mA)
Rdlc vbat_p 0 800
* ---- VBAT sense (R20/R21/C31 on VBAT_F) into PA4 with STM32 clamp diodes to VDDA=3.3 V / VSS
R20 vbat_f vbat_sense 100k
R21 vbat_sense 0 10k
C31 vbat_sense 0 100n
Dclp vbat_sense vdda DCLAMP
Dcln 0 vbat_sense DCLAMP
.model DCLAMP D(Is=1e-14 N=1.05 Rs=50)
Vdda vdda 0 3.3
"""


def deck(src, tran, vbr=30.4, extra=''):
    return '\n'.join([src, tvs(vbr), PATH, extra, '.options reltol=1e-4 method=gear itl4=100', tran])


def transient(tag, src, tstop, tstep, vbr, vecs='v(vbat_in) v(vbat_f) v(vbat_p) v(vbat_sense) i(Vs) v(buck_en) v(tf1)'):
    f = os.path.join(OUT, tag + '.dat')
    body = deck(src, f'.tran {tstep} {tstop} 0 {tstep}', vbr,
                '.control\nrun\nwrdata ' + f + ' ' + vecs + '\n.endc')
    run(tag, body)
    d = np.loadtxt(f)
    return d[:, 0], {n: d[:, 2 * i + 1] for i, n in enumerate(['vin', 'vf', 'vp', 'vs', 'is', 'en', 'tf1'])}


res = {}
# (a) reverse battery: DC, -12 and -16 V (source 0.01 ohm), plus -24 V (reversed jump-start pack)
for vr in (-12, -16, -24):
    t, w = transient(f'in_rev{vr}', f'Vs src 0 PWL(0 0 1m {vr})\nRs src vbat_in 0.01', '20m', '20u', 28.9)
    k = t > 15e-3
    res[f'reverse_{vr}V'] = dict(I_board_mA=float(-w['is'][k].mean() * 1e3), vbat_p=float(w['vp'][k].mean()),
                                vbat_sense=float(w['vs'][k].mean()),
                                pa4_inject_uA=float(((w['vs'][k] - 0) / 1e9).mean()))
    print('rev', vr, res[f'reverse_{vr}V'])

# (b) ISO 7637-2 5b (suppressed load dump): 13.5 V -> Us* 35 V, td 400 ms, Ri 0.5 / 4 ohm; TVS VBR corners
for ri in (0.5, 4.0):
    for vbr in (28.9, 30.4, 31.9):
        src = (f'Vs src 0 PWL(0 13.5 50m 13.5 55m 35 455m 35 505m 13.5 900m 13.5)\nRs src vbat_in {ri}')
        t, w = transient(f'in_5b_ri{ri}_vbr{vbr}', src, '0.9', '50u', vbr)
        pt = (w['vin'] - w['vf']) * 0  # placeholder
        i_tvs = -w['is'] - 0  # source current
        # TVS current = source current - board load (approximate board load from vbat_p consumers)
        r = dict(vbat_f_max=float(w['vf'].max()), vbat_p_max=float(w['vp'].max()),
                 i_src_max=float((-w['is']).max()), ptc_T_max=float(w['tf1'].max()),
                 vbat_sense_max=float(w['vs'].max()))
        res[f'5b_Ri{ri}_VBR{vbr}'] = r
        print('5b', ri, vbr, r)

# separate run with TVS current/power vectors for the energy number (worst: Ri 0.5, VBR min)
for vbr in (28.9, 30.4, 31.9):
    f = os.path.join(OUT, f'in_5b_tvsI_{vbr}.dat')
    src = 'Vs src 0 PWL(0 13.5 50m 13.5 55m 35 455m 35 505m 13.5 900m 13.5)\nRs src vbat_in 0.5'
    body = deck(src, '.tran 50u 0.9 0 50u', vbr, '.control\nrun\nwrdata ' + f + ' i(Vtvs) v(vbat_f) v(tf1) v(vbat_p) v(vbat_in)\n.endc')
    run(f'in_5b_tvsI_{vbr}', body)
    d = np.loadtxt(f); t = d[:, 0]; it = np.abs(d[:, 1]); vf = d[:, 3]
    p = it * np.abs(vf)
    e = float(np.trapezoid(p, t))
    ptc_trip_t = None
    ipk = it.max(); k = np.nonzero((t > 60e-3) & (t < 455e-3) & (it < 0.1 * ipk))[0]
    if len(k):
        ptc_trip_t = float(t[k[0]] - 55e-3)
    res[f'5b_tvs_VBR{vbr}'] = dict(I_tvs_peak=float(it.max()), P_tvs_peak=float(p.max()), E_tvs_J=e,
                                  t_ptc_trip_s=ptc_trip_t, vbat_f_max=float(vf.max()))
    print('5b tvs', vbr, res[f'5b_tvs_VBR{vbr}'])

# (b2) ISO 7637-2 pulse 2a (+112 V max, Ri 2 ohm, 1 us rise, 50 us) and 3b (+150 V, Ri 50 ohm, 5 ns/150 ns),
#      and pulse 1 (-150 V, Ri 10 ohm, 1 us / 2 ms) on top of 13.5 V
LISN = 'Vb bsrc 0 13.5\nLlisn bsrc bl 5u\nRlisn bl vbat_in 0.01\n'
pulses = {
    # generator in parallel with the battery (battery behind the 5 uH artificial network), current sensed in Vs
    '2a_112V': (LISN + 'Vs src 0 PWL(0 0 100u 0 101u 112 150u 0 400u 0)\nRs src vbat_in 2', '400u', '20n'),
    '3b_150V': (LISN + 'Vs src 0 PWL(0 0 10u 0 10.005u 150 10.15u 0 30u 0)\nRs src vbat_in 50', '30u', '1n'),
    # pulse 1: supply disconnected, DUT sees only the generator (Ri 10 ohm) for the pulse
    '1_-150V': ('Vs src 0 PWL(0 13.5 100u 13.5 101u -136.5 2.1m 13.5 3m 13.5)\nRs src vbat_in 10', '3m', '50n'),
}
for nm, (src, ts, st) in pulses.items():
    for vbr in (28.9, 31.9):
        t, w = transient(f'in_p{nm}_{vbr}', src, ts, st, vbr)
        r = dict(vbat_f_max=float(w['vf'].max()), vbat_f_min=float(w['vf'].min()), vbat_p_max=float(w['vp'].max()),
                 vbat_p_min=float(w['vp'][t > 5e-6].min()), q1_vds_max=float((w['vp'] - w['vf']).max()),
                 i_src_peak=float(np.abs(w['is']).max()))
        res[f'pulse_{nm}_VBR{vbr}'] = r
        print('pulse', nm, vbr, r)

# (c) cold crank: 12 V -> 6 V (100 ms) -> 7 V (1 s) -> 12 V, and slow ramp for start/stop thresholds
src = 'Vs src 0 PWL(0 12 50m 12 52m 6 152m 6 160m 7 1.16 7 1.2 12 1.5 12)\nRs src vbat_in 0.02'
t, w = transient('in_crank', src, '1.5', '100u', 30.4)
on = w['en'] > 1.2
res['crank_6V'] = dict(en_min=float(w['en'].min()), buck_on_whole_time=bool(on[t > 10e-3].all()),
                       buck_off_s=float(((~on) & (t > 10e-3)).sum() * 100e-6))
print('crank', res['crank_6V'])
src = 'Vs src 0 PWL(0 0 1 14 2 3)\nRs src vbat_in 0.02'
t, w = transient('in_uvlo_ramp', src, '2', '200u', 30.4)
up = t < 1; dn = t >= 1
on = w['en'] > 1.2
i_start = np.nonzero(on & up)[0][0]; i_stop = np.nonzero((~on) & dn)[0][0]
res['buck_uvlo_sim_typ'] = dict(vstart=float(w['vp'][i_start]), vstop=float(w['vp'][i_stop]),
                                en_at_42V=None)
print('uvlo', res['buck_uvlo_sim_typ'])

# analytic EN corners (TPS54360B DS: EN 1.1/1.2/1.3 V, I1 0.58/1.2/1.8 uA, Ihys 2.2/3.4/4.5 uA)
R1, R2 = 510e3, 84.5e3
Rp = R1 * R2 / (R1 + R2); k = (R1 + R2) / R2
def vstart(ven, i1): return (ven - i1 * Rp) * k
def vstop(ven, i1, ih): return (ven - (i1 + ih) * Rp) * k
res['buck_uvlo_analytic'] = dict(
    start_typ=vstart(1.2, 1.2e-6), stop_typ=vstop(1.2, 1.2e-6, 3.4e-6),
    start_max=vstart(1.3, 0.58e-6), start_min=vstart(1.1, 1.8e-6),
    stop_max=vstop(1.3, 0.58e-6, 2.2e-6), stop_min=vstop(1.1, 1.8e-6, 4.5e-6),
    en_pin_at_42V=42 / k + 4.6e-6 * Rp, en_pin_at_42V_abs_max=8.4)
print(res['buck_uvlo_analytic'])

# 7: VBAT_SENSE DC at 24 / 33 / 42 V and injection
for vb in (16, 24, 33, 36.3, 42):
    th = vb * 10 / 110; rth = 100e3 * 10e3 / 110e3
    inj = max(0, (th - 3.6) / rth)
    res[f'sense_{vb}V'] = dict(pin_open=th, inj_uA_if_clamped_at_3V6=inj * 1e6)
json.dump(res, open(os.path.join(OUT, 'input.json'), 'w'), indent=1, default=str)
