"""Deck generator + runner for the OBD adapter front-end sims.

Every element below is transcribed from audit/netlist.txt (refs in comments).
Usage: imported by the sim_*.py scripts; run with python3 -I.
"""
import os, re, subprocess

SIM = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(SIM, 'lib', 'parts.lib')
DECKS = os.path.join(SIM, 'decks')
OUT = os.path.join(SIM, 'out')
os.makedirs(DECKS, exist_ok=True)
os.makedirs(OUT, exist_ok=True)


def pwl(name, node, edges, v0, v1, tr=10e-9):
    """edges: list of (t, level 0/1). Square logic wave with tr edges."""
    pts = [(0.0, v0 if edges[0][1] == 0 else v1)] if edges[0][0] > 0 else []
    lvl = None
    for t, l in edges:
        if lvl is None:
            pts.append((t, v1 if l else v0))
        else:
            pts.append((t, v1 if lvl else v0))
            pts.append((t + tr, v1 if l else v0))
        lvl = l
    s = ' '.join(f'{t:.9g} {v:.6g}' for t, v in pts)
    return f'V{name} {node} 0 PWL({s})'


def frontend(q2='MMBT5401', q8='MMBT3904', lm317_vdo=1.6, dlc_en=3.3, vsel=0.0, iadj_note=True,
             k_load=True, l_load=True, j1850=True):
    """DLC_SW switch, LM317L supply, J1850 Bus+/Bus- TX, LM339 RX, K/L lines.  Needs nodes
    vbat_p, 5v, 3v3 and the logic inputs j1850_txp, j1850_txn, kline_tx, lline_tx defined by caller."""
    L = []
    a = L.append
    a('* ===== dlcsw block =====')
    a(f'Q2 dlc_sw dlc_sw_b vbat_p {q2}            ; Q2 MMBT5401 C=DLC_SW B=DLC_SW_B E=VBAT_P')
    a('R26 vbat_p dlc_sw_b 10k                     ; R26')
    a('R27 dlc_sw_b dlc_sw_d 4.7k                  ; R27')
    a('XQ3 dlc_sw_d dlc_sw_en 0 N2N7002            ; Q3 2N7002 D G S')
    a('R28 dlc_sw_en 0 100k                        ; R28')
    a(f'VEN dlc_sw_en_src 0 {dlc_en}')
    a('Ren dlc_sw_en_src dlc_sw_en 50              ; MCU PC10 drive')
    a('C36 dlc_sw 0 1u                             ; C36')
    a('C37 dlc_sw 0 100n                           ; C37')
    a('C39 dlc_sw 0 100n                           ; C39')
    a('ILM339 dlc_sw 0 1.4m                        ; U12 supply current (0.8 typ / 2 max)')
    if j1850:
        a('* ===== j1850 block =====')
        a(f'XU11 dlc_sw vj1850 vj_adj LM317L params: vref=1.25 vdo={lm317_vdo}   ; U11 LM317LIDR')
        a('C38 vj1850 0 1u                             ; C38')
        a('R29 vj1850 vj_adj 240                       ; R29')
        a('R30 vj_adj vj_n1 866                        ; R30')
        a('R31 vj_n1 0 374                             ; R31')
        a('XQ4 vj_n1 j1850_vsel 0 N2N7002              ; Q4 (PWM select shorts R31)')
        a('R32 j1850_vsel 0 10k                        ; R32')
        a(f'VVSEL j1850_vsel 0 {vsel}                   ; PB2')
        a('XQ5 j1850_pg j1850_txp 0 N2N7002            ; Q5')
        a('R33 j1850_txp 0 10k                         ; R33')
        a('R34 vj1850 j1850_pg 1.5k                    ; R34')
        a('XQ6 j1850_pd j1850_pg vj1850 AO3401A        ; Q6 AO3401A D G S')
        a('D10 j1850_pd j1850_p B5819W                 ; D10 A=PD K=J1850_P')
        a('R35 j1850_p j1850_pload 10k                 ; R35')
        a('D11 j1850_pload 0 B5819W                    ; D11 A=PLOAD K=GND')
        a('D17 0 j1850_p SMF24A                        ; D17 SMF24A (A1=J1850_P is cathode-side; unidirectional)')
        a('Q7 j1850_n j1850_nb 0 MMBT3904              ; Q7 MMBT3904 C B E')
        a('R36 j1850_txn j1850_nb 1k                   ; R36')
        a('R37 j1850_txn 0 100k                        ; R37')
        a('D12 5v j1850_npu B5819W                     ; D12 A=5V K=NPU')
        a('R38 j1850_npu j1850_n 10k                   ; R38')
        a('D18 0 j1850_n SMF24A                        ; D18')
        a('* VPW receiver (U12 unit A: out pin2, - pin4, + pin5)')
        a('R39 j1850_p vpw_in 10k                      ; R39')
        a('D13 0 vpw_in B5819W                         ; D13 A=GND K=VPW_IN')
        a('R40 5v vpw_ref 13k                          ; R40 (was 2.7k)')
        a('R41 vpw_ref 0 47k                           ; R41 (was 10k)')
        a('XU12A vpw_in vpw_ref dlc_sw vpw_rx LM339U')
        a('R42 3v3 vpw_rx 10k                          ; R42')
        a('Cvpwrx vpw_rx 0 10p                         ; MCU pin + trace')
        a('* PWM receiver (U12 unit B: out pin1, - pin6, + pin7)')
        a('R43 j1850_p pwm_inp 10k                     ; R43')
        a('R44 j1850_n pwm_inn 10k                     ; R44')
        a('D14 0 pwm_inp B5819W                        ; D14')
        a('D15 0 pwm_inn B5819W                        ; D15')
        a('XU12B pwm_inp pwm_inn dlc_sw pwm_rx LM339U')
        a('R45 3v3 pwm_rx 10k                          ; R45')
        a('Cpwmrx pwm_rx 0 10p')
    a('* ===== iso block =====')
    if k_load:
        a('R46 dlc_sw kline 510                        ; R46 510R 1W 2512')
        a(f'Q8 kline kline_b 0 {q8}                    ; Q8 MMBT3904')
        a('R47 kline_tx kline_b 1k                     ; R47')
        a('R48 kline_tx 0 100k                         ; R48')
        a('D19 0 kline SMF24A                          ; D19')
        a('R52 kline k_in 10k                          ; R52')
        a('D16 0 k_in B5819W                           ; D16')
        a('R53 dlc_sw k_ref 10k                        ; R53')
        a('R54 k_ref 0 10k                             ; R54')
        a('XU12C k_in k_ref dlc_sw kline_rx LM339U     ; U12 unit C (out 13, - 10, + 11)')
        a('R55 3v3 kline_rx 10k                        ; R55')
        a('Ckrx kline_rx 0 10p')
    if l_load:
        a('R49 dlc_sw lline 510                        ; R49 510R 1W 2512')
        a(f'Q9 lline lline_b 0 {q8}                    ; Q9')
        a('R50 lline_tx lline_b 1k                     ; R50')
        a('R51 lline_tx 0 100k                         ; R51')
        a('D20 0 lline SMF24A                          ; D20')
    return '\n'.join(L)


def run(name, body, timeout=600):
    deck = os.path.join(DECKS, name + '.cir')
    with open(deck, 'w') as f:
        f.write(f'* {name}\n.include {LIB}\n{body}\n.end\n')
    r = subprocess.run(['ngspice', '-b', deck], capture_output=True, text=True, timeout=timeout, cwd=OUT)
    log = r.stdout + r.stderr
    with open(os.path.join(OUT, name + '.log'), 'w') as f:
        f.write(log)
    meas = {}
    for m in re.finditer(r'^\s*([a-z0-9_]+)\s*=\s*([-+0-9.eE]+)', r.stdout, re.M):
        try:
            meas[m.group(1)] = float(m.group(2))
        except ValueError:
            pass
    fails = re.findall(r'^\s*([a-z0-9_]+)\s*=?\s*failed', log, re.M)
    for k in fails:
        meas.setdefault(k, None)
    if 'error' in log.lower() and not meas:
        print(log[-3000:])
    return meas, log


def read_wrdata(path):
    import numpy as np
    d = np.loadtxt(path)
    return d
