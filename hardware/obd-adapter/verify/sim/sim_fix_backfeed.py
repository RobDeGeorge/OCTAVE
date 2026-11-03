"""Fix check: series diode (1N4148W-class, modelled) in the K/L pull-ups (DLC_SW -> D -> 510R -> line)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen, sim_kline
orig = gen.frontend
def patched(**kw):
    s = orig(**kw)
    s = s.replace('R46 dlc_sw kline 510', 'DFIXK dlc_sw kfx D4148\nR46 kfx kline 510')
    s = s.replace('R49 dlc_sw lline 510', 'DFIXL dlc_sw lfx D4148\nR49 lfx lline 510')
    return s + '\n.model D4148 D(Is=2.5n N=1.75 Rs=0.6 Bv=100 Cjo=2p)'
sim_kline.frontend = patched
for vb in (12, 16):
    for recu in (510, 5100):
        r = sim_kline.kline_case(vb, recu, 10e-9, tag=f'kfix_off_{vb}_{recu}', mode='ecu', dlc_en=0)
        print('FIX OFF', vb, recu, 'k_idle=%.2f dlc_sw=%.2f edges_ok=%s' % (r['k_idle'], r['dlc_sw_idle'], r['edges_ok']))
for vb in (9, 12, 16):
    r = sim_kline.kline_case(vb, None, 10e-9, tag=f'kfix_on_{vb}')
    print('FIX ON', vb, 'k_idle=%.2f vol=%.2f tr=%.2f werr=%s' % (r['k_idle'], r['k_vol'], r['t_rise_20_80_us'], r.get('width_err_pct_bit')))
