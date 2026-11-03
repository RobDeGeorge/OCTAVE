"""Sims 1 + 2: J1850 VPW (TX + RX, listen case) and J1850 PWM."""
import json, sys
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from gen import frontend, run, pwl, OUT
import os

V5 = float(os.environ.get('V5', '4.62'))   # "5V" net = 5.00 V buck - SS34 (D6) drop at ~0.4 A


def edges_from_widths(t0, widths, first=1):
    e, t, l = [(0, 0)], t0, first
    for w in widths:
        e.append((t, l)); t += w; l ^= 1
    e.append((t, 0))
    return e, t


VPW_W = [200e-6, 64e-6, 128e-6, 128e-6, 64e-6, 64e-6, 64e-6, 128e-6, 128e-6, 64e-6, 64e-6]


def analyse_edges(t, v, thr):
    s = v > thr
    idx = np.nonzero(s[1:] != s[:-1])[0]
    tc = []
    for i in idx:
        # linear interpolate
        t_ = t[i] + (thr - v[i]) * (t[i + 1] - t[i]) / (v[i + 1] - v[i])
        tc.append((t_, bool(s[i + 1])))
    return tc


def vpw_case(vbat, rl, cl, tag, listen=False, q2='MMBT5401', v5=V5, plot=False):
    T0 = 1e-3
    e, tend = edges_from_widths(T0 + 20e-6, VPW_W)
    body = []
    body.append(f'VBAT vbat_src 0 {vbat}\nRbat vbat_src vbat_p 0.05')
    body.append(f'V5 5v 0 {v5}\nV33 3v3 0 3.3')
    body.append(frontend(q2=q2))
    body.append('VKTX kline_tx 0 0\nVLTX lline_tx 0 0\nVTXN j1850_txn 0 0')
    # rest of bus (other nodes): lumped load
    body.append(f'RLOAD j1850_p 0 {rl}\nCLOAD j1850_p 0 {cl}')
    if listen:
        body.append('VTXP j1850_txp 0 0')
        # another node (AU5780-like): 7.6 V, slew-limited 0.3 V/us source through 10 ohm + diode
        pts = ['0 0']
        for (t0, l) in e[1:]:
            pts.append(f'{t0:.9g} {0 if l else 7.6}')
            pts.append(f'{t0+25e-6:.9g} {7.6 if l else 0}')
        body.append('Vext ext 0 PWL(' + ' '.join(pts) + ')')
        body.append('Rext ext ext2 10\nDext ext2 j1850_p B5819W')
    else:
        body.append(pwl('TXP', 'j1850_txp', e, 0, 3.3))
    body.append('.options reltol=1e-4 abstol=1e-10 method=gear')
    body.append(f'.tran 20n {tend+250e-6:.6g} 0 50n')
    f = os.path.join(OUT, f'{tag}.dat')
    body.append('.control\nrun\nwrdata ' + f + ' v(j1850_txp) v(j1850_p) v(vpw_rx) v(vj1850) v(dlc_sw) v(vpw_in) v(vpw_ref) v(pwm_rx)\n.endc')
    meas, log = run(tag, '\n'.join(body))
    d = np.loadtxt(f)
    t = d[:, 0]; tx = d[:, 1]; bus = d[:, 3]; rx = d[:, 5]; vj = d[:, 7]; dlc = d[:, 9]
    # bus high level: during the SOF last 30 us
    sof = (t > T0 + 190e-6) & (t < T0 + 218e-6)
    post = t > T0 - 100e-6
    res = dict(vbat=vbat, rl=rl, cl=cl, listen=listen,
               vj1850=float(vj[sof].mean()), dlc_sw=float(dlc[sof].mean()), bus_high=float(bus[sof].mean()),
               rx_max=float(rx[post].max()), rx_min=float(rx[post].min()), vpw_ref=float(d[sof, 13].mean()))
    # rise / fall 1.5 V <-> 6.25 V (AU5780 slew window) on first SOF edge and its falling edge
    def cross(thr, rising, after):
        for tc, up in analyse_edges(t, bus, thr):
            if tc > after and up == rising:
                return tc
        return None
    r1, r2 = cross(1.5, True, T0), cross(min(6.25, 0.9 * res['bus_high']), True, T0)
    f1 = cross(min(6.25, 0.9 * res['bus_high']), False, T0 + 100e-6); f2 = cross(1.5, False, T0 + 100e-6)
    res['t_rise_1.5_6.25'] = (r2 - r1) if (r1 and r2) else None
    res['t_fall_6.25_1.5'] = (f2 - f1) if (f1 and f2) else None
    # pulse widths: driver reference (TX or ext) vs RX (threshold 1.65 V)
    ref_edges = [x for x in e[1:]]
    rx_edges = [x for x in analyse_edges(t, rx, 1.65) if x[0] > T0]
    res['n_tx_edges'] = len(ref_edges); res['n_rx_edges'] = len(rx_edges)
    ok = len(rx_edges) == len(ref_edges)
    res['rx_edge_count_ok'] = ok
    if ok:
        dl = [rx_edges[i][0] - ref_edges[i][0] for i in range(len(ref_edges))]
        res['delay_rise_us'] = [round(x * 1e6, 2) for x, (_, u) in zip(dl, rx_edges) if u]
        res['delay_fall_us'] = [round(x * 1e6, 2) for x, (_, u) in zip(dl, rx_edges) if not u]
        w_rx = np.diff([x[0] for x in rx_edges])
        w_tx = np.diff([x[0] for x in ref_edges])
        res['width_err_us_max'] = float(np.max(np.abs(w_rx - w_tx)) * 1e6)
        # J1850 VPW receive windows: short 34..96 us, long 96..163, SOF 163..239
        def cls(w):
            return 'S' if 34e-6 < w <= 96e-6 else 'L' if 96e-6 < w <= 163e-6 else 'SOF' if 163e-6 < w <= 239e-6 else 'BAD'
        res['symbols_ok'] = [cls(a) for a in w_rx] == [cls(b) for b in w_tx]
    if plot:
        res['_dat'] = f
    return res


def pwm_case(vbat, rl, cl, tag, v5=V5):
    # PWM: 24 us bit, '1' = 8 us active, '0' = 16 us active; SOF 32 us active + 16 passive
    bits = [1, 0, 1, 1, 0, 0, 1, 0]
    T0 = 1e-3
    e = [(0, 0)]; t = T0 + 20e-6
    e.append((t, 1)); t += 32e-6; e.append((t, 0)); t += 16e-6
    for b in bits:
        e.append((t, 1)); t += (8e-6 if b else 16e-6); e.append((t, 0)); t += (16e-6 if b else 8e-6)
    tend = t + 40e-6
    body = [f'VBAT vbat_src 0 {vbat}\nRbat vbat_src vbat_p 0.05', f'V5 5v 0 {v5}\nV33 3v3 0 3.3',
            frontend(vsel=3.3), 'VKTX kline_tx 0 0\nVLTX lline_tx 0 0',
            pwl('TXP', 'j1850_txp', e, 0, 3.3), pwl('TXN', 'j1850_txn', e, 0, 3.3),
            # network: Bus+ load to GND, Bus- load to 5 V, each line CL
            f'RLP j1850_p 0 {rl}\nCLP j1850_p 0 {cl}\nRLN j1850_n 5v {rl}\nCLN j1850_n 0 {cl}',
            '.options reltol=1e-4 method=gear', f'.tran 10n {tend:.6g} 0 20n']
    f = os.path.join(OUT, f'{tag}.dat')
    body.append('.control\nrun\nwrdata ' + f + ' v(j1850_txp) v(j1850_p) v(j1850_n) v(pwm_rx) v(vj1850)\n.endc')
    run(tag, '\n'.join(body))
    d = np.loadtxt(f)
    tt = d[:, 0]; bp = d[:, 3]; bn = d[:, 5]; rx = d[:, 7]; vj = d[:, 9]
    sof = (tt > T0 + 45e-6) & (tt < T0 + 51e-6)
    idle = (tt > T0 - 20e-6) & (tt < T0 + 19e-6)
    post = tt > T0 - 100e-6
    res = dict(vbat=vbat, rl=rl, cl=cl, vj1850=float(vj[sof].mean()), busp_high=float(bp[sof].mean()),
               busn_low=float(bn[sof].mean()), busp_idle=float(bp[idle].mean()), busn_idle=float(bn[idle].mean()),
               rx_max=float(rx[post].max()))
    rx_e = [x for x in analyse_edges(tt, rx, 1.65) if x[0] > T0]
    ref = e[1:]
    res['edge_count_ok'] = len(rx_e) == len(ref)
    if res['edge_count_ok']:
        dl = [rx_e[i][0] - ref[i][0] for i in range(len(ref))]
        res['delay_rise_us_max'] = max(x for x, (_, u) in zip(dl, rx_e) if u) * 1e6
        res['delay_fall_us_max'] = max(x for x, (_, u) in zip(dl, rx_e) if not u) * 1e6
        w_rx = np.diff([x[0] for x in rx_e]); w_tx = np.diff([x[0] for x in ref])
        res['width_err_us_max'] = float(np.max(np.abs(w_rx - w_tx)) * 1e6)
    return res


if __name__ == '__main__':
    out = {'vpw': [], 'vpw_listen': [], 'pwm': []}
    loads = [('light', 10.6e3, 470e-12), ('spec_heavyR', 300, 16e-9), ('spec_tau1.6k', 1.6e3, 3.3e-9),
             ('task_1.6k_16n', 1.6e3, 16e-9), ('stress_10.6k_16n', 10.6e3, 16e-9)]
    for vb in [9, 12, 16, 24]:
        for nm, rl, cl in loads:
            r = vpw_case(vb, rl, cl, f'vpw_{vb}V_{nm}', plot=(nm in ('light', 'task_1.6k_16n')))
            r['load'] = nm; out['vpw'].append(r); print('VPW', vb, nm, {k: v for k, v in r.items() if not k.startswith('_')})
        for nm, rl, cl in [loads[0], loads[1], loads[3]]:
            r = vpw_case(vb, rl, cl, f'vpwlisten_{vb}V_{nm}', listen=True)
            r['load'] = nm; out['vpw_listen'].append(r); print('LISTEN', vb, nm, r)
        for nm, rl, cl in [('light', 10.6e3, 470e-12), ('mid', 1.6e3, 2.2e-9), ('heavy', 300, 3.3e-9)]:
            r = pwm_case(vb, rl, cl, f'pwm_{vb}V_{nm}')
            r['load'] = nm; out['pwm'].append(r); print('PWM', vb, nm, r)
    # 5 V rail corner for the VPW threshold (R40/R41 from "5V")
    for v5 in (4.42, 4.62, 5.0):
        r = vpw_case(12, 1.6e3, 3.3e-9, f'vpw_v5_{v5}', v5=v5)
        r['load'] = f'v5={v5}'; out.setdefault('vpw_v5', []).append(r); print('V5', v5, r)
    json.dump(out, open(os.path.join(OUT, 'j1850.json'), 'w'), indent=1, default=str)
