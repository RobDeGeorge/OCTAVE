"""Sim 6: TPS54360B (U7) averaged peak-current-mode model: loop gain (AC), startup + load step (tran).
TI's TPS54360B transient model is an encrypted PSpice model that ngspice cannot run, so this is the
datasheet small-signal model (Fig. 29/30, Eq. 11-24) plus Ridley's sampling double pole at fsw/2."""
import sys, os, json, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from gen import run, OUT

FSW = 92417 * 160 ** -0.991 * 1e3          # DS Eq. 7 with RT = 160k
GM_EA, AOL, UGBW, GM_PS = 350e-6, 10000, 2.5e6, 12.0
RO = AOL / GM_EA; CO = GM_EA / (2 * math.pi * UGBW)
R16, R17 = 68e3, 12e3                      # the board's FB divider (gen/obd_adapter.py)
VOUT = 0.8 * (1 + R16 / R17)


def deck(vin, cout, esr, iload, mc, analysis, tag, istep=None):
    d = (VOUT + 0.45) / (vin + 0.45)        # diode-rectified duty
    q = 1 / (math.pi * max(mc * (1 - d) - 0.5, 0.02))
    wn = math.pi * FSW
    # 2nd-order sampling filter as series R-L-C driven by a buffer: wn = 1/sqrt(LC), Q = sqrt(L/C)/R
    C = 1e-9; L = 1 / (wn ** 2 * C); R = math.sqrt(L / C) / q
    lines = [f'* vin={vin} d={d:.3f} Q={q:.2f} fsw={FSW:.0f}',
             'Vref ref 0 PWL(0 0 2.1m 0.8)' if analysis == 'tran' else 'Vref ref 0 0.8',
             # error amp gm into COMP
             f'Gea 0 comp ref fbx {GM_EA}', f'Ro comp 0 {RO}', f'Co comp 0 {CO}',
             'R18 comp comp_rc 13k        ; R18', 'C22 comp_rc 0 6.8n          ; C22', 'C23 comp 0 47p              ; C23',
             # sampling double pole
             'Ebuf cb 0 comp 0 1', f'Rs cb s1 {R}', f'Ls s1 s2 {L}', f'Cs s2 0 {C}',
             # power stage: inductor current = gmps*(Vc - 0.6), limited 0..5.5 A (DS current limit typ)
             f'Bps 0 vout_n I = max(0,min(5.5,{GM_PS}*(V(s2)-0.6)))',
             f'Cout vout_n esr_n {cout}', f'Resr esr_n 0 {esr}',
             f'R16 vout_n fb {R16}         ; R16', f'R17 fb 0 {R17}              ; R17']
    if analysis == 'ac':
        lines += ['Vinj fb fbx DC 0 AC 1', f'Rload vout_n 0 {VOUT / iload}', '.ac dec 200 10 1e6',
                  '.control\nrun\nlet T = -v(fb)/v(fbx)\nwrdata ' + os.path.join(OUT, tag + '.dat') +
                  ' db(T) ph(T)\n.endc']
    else:
        lines += ['Vinj fb fbx 0', f'Iload vout_n 0 PWL(0 {iload} 4m {iload} 4.001m {istep} 6m {istep} 6.001m {iload})',
                  '.tran 0.5u 8m 0 0.5u', '.control\nrun\nwrdata ' + os.path.join(OUT, tag + '.dat') + ' v(vout_n)\n.endc']
    return '\n'.join(lines)


def loop(vin, cout, iload, mc, esr=1.5e-3):
    tag = f'buck_ac_{vin}_{cout*1e6:.0f}u_{iload}_{mc}'
    run(tag, deck(vin, cout, esr, iload, mc, 'ac', tag))
    d = np.loadtxt(os.path.join(OUT, tag + '.dat'))
    f, db, ph = d[:, 0], d[:, 1], np.degrees(d[:, 3]) if np.abs(d[:, 3]).max() < 7 else d[:, 3]
    i = np.nonzero(db < 0)[0][0]
    fc = f[i]; pm = 180 + ph[i]
    pm = (pm + 180) % 360 - 180
    # gain margin: where phase crosses -180
    ph_w = np.unwrap(np.radians(ph))
    j = np.nonzero(np.degrees(ph_w) < -180)[0]
    gm = -db[j[0]] if len(j) else None
    return dict(vin=vin, cout_uF=cout * 1e6, iload=iload, mc=mc, fc_kHz=fc / 1e3, pm_deg=float(pm), gm_dB=gm)


if __name__ == '__main__':
    res = dict(fsw_kHz=FSW / 1e3, vout=VOUT, ro=RO, co=CO, loop=[], step=[])
    print(res)
    for vin in (6, 9, 12, 16, 24, 42):
        for cout in (25e-6, 40e-6, 60e-6):
            for mc in (1.5, 3.0):
                for il in (0.1, 1.5):
                    r = loop(vin, cout, il, mc); res['loop'].append(r); print(r)
    for vin in (12, 16):
        for cout in (25e-6, 40e-6):
            tag = f'buck_step_{vin}_{cout*1e6:.0f}u'
            run(tag, deck(vin, cout, 1.5e-3, 0.1, 2.0, 'tran', tag, istep=1.5))
            d = np.loadtxt(os.path.join(OUT, tag + '.dat')); t, v = d[:, 0], d[:, 1]
            k1 = (t > 4e-3) & (t < 6e-3); k2 = t > 6e-3; ss = (t > 3.5e-3) & (t < 4e-3)
            r = dict(vin=vin, cout_uF=cout * 1e6, v_ss=float(v[ss].mean()), undershoot_mV=float((v[ss].mean() - v[k1].min()) * 1e3),
                     overshoot_mV=float((v[k2].max() - v[ss].mean()) * 1e3),
                     startup_overshoot_mV=float((v[t < 3.5e-3].max() - v[ss].mean()) * 1e3))
            res['step'].append(r); print(r)
    # ripple / peak current / diode
    L = 8.2e-6
    res['ripple'] = []
    for vin in (9, 12, 16, 24, 42):
        d = (VOUT + 0.45) / (vin + 0.45)
        di = (vin - VOUT) * d / (L * FSW)
        res['ripple'].append(dict(vin=vin, duty=d, dI_pp_A=di, Ipk_at_1p5A=1.5 + di / 2, Ipk_at_0p6A=0.6 + di / 2,
                                  diode_avg_A_at_1p5A=1.5 * (1 - d), diode_P_W=1.5 * (1 - d) * 0.45))
        print(res['ripple'][-1])
    json.dump(res, open(os.path.join(OUT, 'buck.json'), 'w'), indent=1, default=str)
