"""Sim 4: DLC_SW high side (Q2 MMBT5401) at worst-case load; DC operating points."""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen import frontend, run


def case(vbat, q2, load, tag):
    body = [f'VBAT vbat_p 0 {vbat}', 'V5 5v 0 4.62\nV33 3v3 0 3.3', frontend(q2=q2)]
    j = 3.3 if load == 'worst' else 0
    kl = 3.3 if load in ('worst', 'kl') else 0
    body.append(f'VTXP j1850_txp 0 {j}\nVTXN j1850_txn 0 0\nVKTX kline_tx 0 {kl}\nVLTX lline_tx 0 {kl}')
    body.append('RLOAD j1850_p 0 300      ; heaviest legal VPW bus (300 ohm) driven high by us')
    body.append('.options gmin=1e-12\n.op')
    body.append('.control\nrun\n'
                'let vce = v(vbat_p)-v(dlc_sw)\nlet ic = -i(VBAT)\n'
                'let ib = (v(dlc_sw_b)-v(dlc_sw_d))/4.7k - (v(vbat_p)-v(dlc_sw_b))/10k\n'
                'let pr27 = (v(dlc_sw_b)-v(dlc_sw_d))^2/4.7k\n'
                'let pq2 = vce*(ic-ib)\n'
                'print vce ic ib pr27 pq2 v(dlc_sw) v(vj1850) v(j1850_p) v(kline) v(lline)\n.endc')
    m, log = run(tag, '\n'.join(body))
    return dict(vbat=vbat, q2=q2, load=load, **m)


if __name__ == '__main__':
    out = []
    for vb in (6, 9, 12, 16, 24, 33, 42):
        for q2 in ('MMBT5401', 'MMBT5401MIN25', 'MMBT5401MIN'):
            for load in ('idle', 'kl', 'worst'):
                r = case(vb, q2, load, f'dlc_{vb}_{q2}_{load}')
                out.append(r); print(r)
    json.dump(out, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out', 'dlcsw.json'), 'w'), indent=1)
