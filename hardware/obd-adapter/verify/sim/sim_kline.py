"""Sim 3: K-line ISO 9141-2 / 14230 at 10400 baud, plus DLC_SW-off back-feed and listen case."""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from gen import frontend, run, pwl, OUT
from sim_j1850 import analyse_edges

BIT = 1 / 10400.0


def line_bits():
    # line levels (1 = recessive/high): 0x55 frame then 0x00 frame then 0xFF frame, LSB first, 8N1
    out = []
    for byte in (0x55, 0x00, 0xF0):
        out.append(0)
        out += [(byte >> i) & 1 for i in range(8)]
        out.append(1)
    return out


def kline_case(vbat, recu, ccab, q8='MMBT3904', q2='MMBT5401', tag='k', mode='tx', dlc_en=3.3):
    T0 = 1e-3
    bits = line_bits()
    e = [(0, 0)]; t = T0; prev = 1
    for b in bits:
        e.append((t, 1 - b)); t += BIT   # TX pin = inverted line level
    e.append((t, 0)); tend = t + 300e-6
    body = [f'VBAT vbat_src 0 {vbat}\nRbat vbat_src vbat_p 0.05', 'V5 5v 0 4.62\nV33 3v3 0 3.3',
            frontend(q2=q2, q8=q8, dlc_en=dlc_en),
            'VTXP j1850_txp 0 0\nVTXN j1850_txn 0 0\nVLTX lline_tx 0 0',
            f'CCAB kline 0 {ccab}']
    if recu:
        body.append(f'RECU kline vbat_src {recu}     ; ECU-side pull-up to battery')
    if mode == 'tx':
        body.append(pwl('KTX', 'kline_tx', e, 0, 3.3))
    else:  # ECU transmits: open-collector pulling K low through 20 ohm, our TX idle
        body.append('VKTX kline_tx 0 0')
        ee = [(0, 0)] + [(tt, l) for tt, l in e[1:]]
        body.append(pwl('ECU', 'ecu_drv', ee, 0, 1))
        body.append('SECU kline 0 ecu_drv 0 SWECU\n.model SWECU SW(Ron=20 Roff=100Meg Vt=0.5 Vh=0.05)')
    body.append('.options reltol=1e-4 method=gear')
    body.append(f'.tran 50n {tend:.6g} 0 200n')
    f = os.path.join(OUT, tag + '.dat')
    body.append('.control\nrun\nwrdata ' + f + ' v(kline_tx) v(kline) v(kline_rx) v(dlc_sw) i(VBAT) v(k_ref) v(kline_b)\n.endc')
    run(tag, '\n'.join(body))
    d = np.loadtxt(f)
    tt, kl, rx, dlc, ib = d[:, 0], d[:, 3], d[:, 5], d[:, 7], -d[:, 9]
    res = dict(vbat=vbat, recu=recu, ccab=ccab, q8=q8, mode=mode)
    idle = (tt > T0 - 100e-6) & (tt < T0)
    # longest low: the 0x00 frame (start + 8 zeros) -> bits 10..18
    low = (tt > T0 + 15 * BIT) & (tt < T0 + 18.5 * BIT)
    res['k_idle'] = float(kl[idle].mean()); res['k_vol'] = float(kl[low].mean())
    res['dlc_sw_idle'] = float(dlc[idle].mean()); res['dlc_sw_low'] = float(dlc[low].mean())
    res['vol_pct_vbat'] = 100 * res['k_vol'] / vbat
    res['rx_max'] = float(rx[tt > T0 - 100e-6].max())
    # rise 20->80 % of Vbat (ISO 9141 levels) on the first rising edge
    hi = res['k_idle']
    ups = [x for x, u in analyse_edges(tt, kl, 0.2 * vbat) if u and x > T0]
    up8 = [x for x, u in analyse_edges(tt, kl, min(0.8 * vbat, 0.95 * hi)) if u and x > T0]
    dn8 = [x for x, u in analyse_edges(tt, kl, min(0.8 * vbat, 0.95 * hi)) if not u and x > T0]
    dn2 = [x for x, u in analyse_edges(tt, kl, 0.2 * vbat) if not u and x > T0]
    res['t_rise_20_80_us'] = (up8[0] - ups[0]) * 1e6 if ups and up8 else None
    res['t_fall_80_20_us'] = (dn2[0] - dn8[0]) * 1e6 if dn2 and dn8 else None
    ref = []
    for x in e[1:]:
        if not ref or ref[-1][1] != x[1]:
            ref.append(x)
    ref = ref[1:] if ref and ref[0][1] == 0 else ref
    rxe = [x for x in analyse_edges(tt, rx, 1.65) if x[0] > T0]
    res['edges_ok'] = len(rxe) == len(ref)
    if res['edges_ok']:
        w_rx = np.diff([x[0] for x in rxe]); w_tx = np.diff([x[0] for x in ref])
        res['width_err_pct_bit'] = float(np.max(np.abs(w_rx - w_tx)) / BIT * 100)
        res['delay_us'] = [round((a[0] - b[0]) * 1e6, 2) for a, b in zip(rxe[:2], ref[:2])]
    else:
        res['n_rx_edges'] = len(rxe); res['n_ref'] = len(ref)
    # power with line held low (DC values during the long low)
    res['P_R46_W'] = float(((dlc[low] - kl[low]) ** 2 / 510).mean())
    res['I_Q8_mA'] = float(((dlc[low] - kl[low]) / 510 + ((vbat - kl[low]) / recu if recu else 0)).mean() * 1e3)
    res['P_Q8_mW'] = res['I_Q8_mA'] * res['k_vol']
    return res


if __name__ == '__main__':
    out = []
    for vb in (9, 12, 16, 24):
        for recu in (None, 510):
            for cc in (470e-12, 10e-9):
                for q8 in ('MMBT3904', 'MMBT3904MIN'):
                    r = kline_case(vb, recu, cc, q8=q8, tag=f'k_{vb}_{recu}_{cc:g}_{q8}')
                    out.append(r); print(r)
        r = kline_case(vb, 510, 10e-9, tag=f'klisten_{vb}', mode='ecu'); out.append(r); print('LISTEN', r)
    # DLC_SW off (sleep): what does the adapter do to an ECU-pulled-up K line?
    for vb in (12, 16):
        r = kline_case(vb, 510, 10e-9, tag=f'koff_{vb}', mode='ecu', dlc_en=0); out.append(r); print('OFF', r)
        r = kline_case(vb, 5100, 10e-9, tag=f'koff5k_{vb}', mode='ecu', dlc_en=0); out.append(r); print('OFF5k', r)
    json.dump(out, open(os.path.join(OUT, 'kline.json'), 'w'), indent=1, default=str)
