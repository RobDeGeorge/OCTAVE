import os, numpy as np, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
O = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out'); P = os.path.join(os.path.dirname(O), 'plots')
C = ['#2a78d6', '#eb6834', '#1baf7a', '#eda100', '#6250d6']
plt.rcParams.update({'axes.facecolor': '#fcfcfb', 'figure.facecolor': '#fcfcfb', 'axes.grid': True, 'grid.color': '#e4e3df',
                     'grid.linewidth': 0.6, 'axes.edgecolor': '#b5b3ad', 'lines.linewidth': 1.6, 'font.size': 9,
                     'axes.spines.top': False, 'axes.spines.right': False, 'text.color': '#0b0b0b', 'axes.labelcolor': '#52514e'})
def stack(fn, title, rows, xs=1e6, xl='time (us)', xlim=None, hlines=None):
    fig, ax = plt.subplots(len(rows), 1, sharex=True, figsize=(9, 2.1 * len(rows)))
    for a, (lab, series) in zip(np.atleast_1d(ax), rows):
        for i, (nm, x, y) in enumerate(series):
            a.plot(x * xs, y, color=C[i], label=nm)
        for (yv, txt) in (hlines or {}).get(lab, []):
            a.axhline(yv, color='#8a8883', ls='--', lw=0.9); a.text(a.get_xlim()[0] if xlim is None else xlim[0], yv, ' ' + txt, va='bottom', fontsize=8, color='#52514e')
        a.set_ylabel(lab)
        if len(series) > 1: a.legend(loc='upper right', frameon=False, fontsize=8)
    if xlim: np.atleast_1d(ax)[-1].set_xlim(*xlim)
    np.atleast_1d(ax)[-1].set_xlabel(xl); fig.suptitle(title, x=0.01, ha='left', fontsize=11)
    fig.tight_layout(); fig.savefig(os.path.join(P, fn), dpi=110); plt.close(fig)
def L(f): return np.loadtxt(os.path.join(O, f))
for tag, ttl in [('vpw_12V_spec_tau1.6k', 'J1850 VPW TX, 12 V, bus 1.6 kOhm || 3.3 nF (spec corner, tau 5.3 us)'),
                 ('vpw_12V_task_1.6k_16n', 'J1850 VPW TX, 12 V, bus 1.6 kOhm || 16 nF (tau 26 us, beyond J1850 5.2 us limit)'),
                 ('vpw_9V_spec_heavyR', 'J1850 VPW TX, 9 V, bus 300 Ohm || 16 nF (heaviest legal load)'),
                 ('vpwlisten_12V_task_1.6k_16n', 'J1850 VPW listen (another node drives, our TX idle), 12 V, 1.6 kOhm || 16 nF')]:
    if not os.path.exists(os.path.join(O, tag + '.dat')): continue
    d = L(tag + '.dat'); t = d[:, 0] - 1e-3
    stack(tag + '.png', ttl, [('TX PC8 (V)', [('J1850_TXP', t, d[:, 1])]),
                             ('Bus+ (V)', [('J1850_P', t, d[:, 3]), ('VPW_REF', t, d[:, 13])]),
                             ('RX PC6 (V)', [('VPW_RX', t, d[:, 5])])], xlim=(-20, 1150),
          hlines={'Bus+ (V)': [(6.25, 'VOH min 6.25 V'), (8.0, 'max 8 V')]})
d = L('pwm_12V_mid.dat'); t = d[:, 0] - 1e-3
stack('pwm_12V_mid.png', 'J1850 PWM, 12 V, 1.6 kOhm / 2.2 nF per line', [('TX (V)', [('TXP=TXN', t, d[:, 1])]),
      ('Bus (V)', [('Bus+', t, d[:, 3]), ('Bus-', t, d[:, 5])]), ('RX PC7 (V)', [('PWM_RX', t, d[:, 7])])], xlim=(-5, 260))
for tag, ttl in [('k_12_510_1e-08_MMBT3904', 'K-line TX 10.4 kbaud, 12 V, ECU 510 Ohm pull-up, 10 nF'),
                 ('k_24_510_1e-08_MMBT3904MIN', 'K-line TX, 24 V jump start, ECU 510 Ohm, 10 nF, min-hFE Q8 (VOL rises to 2.65 V)'),
                 ('koff_12', 'K-line with DLC_SW OFF (adapter asleep), ECU 510 Ohm pull-up: K idles at 8.2 V, not 12 V')]:
    d = L(tag + '.dat'); t = d[:, 0] - 1e-3
    stack(tag + '.png', ttl, [('TX PC1 (V)', [('KLINE_TX', t, d[:, 1])]), ('K (V)', [('KLINE', t, d[:, 3]), ('DLC_SW', t, d[:, 7])]),
                             ('RX PC0 (V)', [('KLINE_RX', t, d[:, 5])])], xlim=(-100, 3000))
d = L('in_5b_tvsI_30.4.dat'); t = d[:, 0]
stack('input_5b_loaddump.png', 'ISO 7637-2 pulse 5b (35 V, 400 ms, Ri 0.5 Ohm), TVS VBR typ 30.4 V',
      [('Volts', [('VBAT_IN', t, d[:, 9]), ('VBAT_F', t, d[:, 3]), ('VBAT_P', t, d[:, 7])]), ('TVS current (A)', [('I(D4)', t, np.abs(d[:, 1]))]),
       ('PTC dT (K)', [('F1 temp rise', t, d[:, 5])])], xs=1e3, xl='time (ms)')
d = L('in_p1_-150V_31.9.dat'); t = d[:, 0]
stack('input_pulse1.png', 'ISO 7637-2 pulse 1 (-150 V, Ri 10 Ohm, 2 ms), TVS VBR max', [('Volts', [('VBAT_IN', t, d[:, 1]), ('VBAT_F', t, d[:, 3]), ('VBAT_P', t, d[:, 5])]),
      ('BUCK_EN (V)', [('EN', t, d[:, 11])])], xs=1e3, xl='time (ms)')
d = L('in_rev-16.dat'); t = d[:, 0]
stack('input_reverse16.png', 'Reverse battery -16 V', [('Volts', [('VBAT_IN', t, d[:, 1]), ('VBAT_F', t, d[:, 3]), ('VBAT_P', t, d[:, 5])]),
      ('Board current (mA)', [('I in', t, d[:, 9] * 1e3)])], xs=1e3, xl='time (ms)')
d = L('in_crank.dat'); t = d[:, 0]
stack('input_crank.png', 'Cold crank 12 -> 6 V (100 ms) -> 7 V (1 s): buck EN (1.2 V threshold)', [('Volts', [('VBAT_P', t, d[:, 5])]),
      ('BUCK_EN (V)', [('EN', t, d[:, 11])])], xs=1e3, xl='time (ms)', hlines={'BUCK_EN (V)': [(1.2, 'EN threshold')]})
fig, ax = plt.subplots(2, 1, sharex=True, figsize=(8, 5))
for i, (tag, nm) in enumerate([('buck_ac_12_40u_1.5_1.5', '12 V, 40 uF, mc 1.5'), ('buck_ac_24_25u_1.5_3.0', '24 V, 25 uF, mc 3 (worst)')]):
    d = L(tag + '.dat'); f = d[:, 0]; ph = np.degrees(np.unwrap(d[:, 3]))
    ax[0].semilogx(f, d[:, 1], color=C[i], label=nm); ax[1].semilogx(f, ph, color=C[i])
ax[0].axhline(0, color='#8a8883', lw=0.9); ax[1].axhline(-180, color='#8a8883', lw=0.9)
ax[0].set_ylabel('loop gain (dB)'); ax[1].set_ylabel('phase (deg)'); ax[1].set_xlabel('Hz'); ax[0].legend(frameon=False)
fig.suptitle('TPS54360B averaged loop gain (R18 13k, C22 6.8n, C23 39p)', x=0.01, ha='left'); fig.tight_layout(); fig.savefig(os.path.join(P, 'buck_loop.png'), dpi=110)
print(sorted(os.listdir(P)))
