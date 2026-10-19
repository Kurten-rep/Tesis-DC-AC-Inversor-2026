# -*- coding: utf-8 -*-
"""Figura 21. Simulación del Canal 1: SPWM unipolar a 20 kHz, filtro LC y carga resistiva.

Modelo de interruptores ideales con tiempo muerto: durante td la rama queda abierta y su
voltaje lo fija el diodo que conduce según el signo de la corriente en la inductancia.
Integración de Euler explícita con paso fijo; señales en tiempo discreto.
Paneles: (a) ciclo completo con el promedio por periodo de conmutación, (b) detalle de
0,5 ms cerca del pico, (c) voltaje de salida y corriente en la inductancia.
Salida: ../../Figuras Tesis/Figura_21_simulacion_canal1.png (300 dpi).
"""
import os
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter
from scipy.ndimage import uniform_filter1d

plt.rcParams.update({
    'font.family': 'Liberation Serif', 'font.size': 10, 'mathtext.fontset': 'stix',
    'axes.linewidth': 0.8, 'axes.grid': True, 'grid.color': '#d0d0d0', 'grid.linewidth': 0.5,
})
coma = FuncFormatter(lambda v, p: ('%g' % v).replace('.', ','))
AZUL, ROJO, GRIS = '#1f5fa8', '#c0392b', '#555555'

# Parámetros (valores nominales de diseño)
Vdc_V, ma, f1_Hz, fsw_Hz = 200.0, 0.85, 60.0, 20e3
L_H, C_F, R_ohm = 1.15e-3, 6.8e-6, 22.5     # carga de 640 VA a 120 VRMS
td_s = 800e-9                               # tiempo muerto (0 para el caso ideal)
dt_s = 1.0 / f1_Hz / 400000                 # 400 000 muestras exactas por ciclo (fs = 24 MHz)
ncy = 5
N = int(round(ncy / f1_Hz / dt_s))

t_s = np.arange(N, dtype=np.float64) * dt_s
ref = ma * np.sin(2 * np.pi * f1_Hz * t_s)
tri = 2 * np.abs(2 * np.mod(t_s * fsw_Hz, 1.0) - 1) - 1
cA = ref > tri; cB = -ref > tri             # comandos de cada rama (unipolar)
nd = int(round(td_s / dt_s))
dA = np.r_[np.zeros(nd, bool), cA[:N - nd]]; dB = np.r_[np.zeros(nd, bool), cB[:N - nd]]
hiA = (cA & dA).tolist(); loA = (~cA & ~dA).tolist()   # encendido retrasado td
hiB = (cB & dB).tolist(); loB = (~cB & ~dB).tolist()
assert len(hiA) == N

iL = 0.0; vc = 0.0
V = np.empty(N, dtype=np.float64); VC = np.empty(N, dtype=np.float64); IL = np.empty(N, dtype=np.float64)
for k in range(N):
    vA = Vdc_V if hiA[k] else (0.0 if loA[k] else Vdc_V * (iL < 0))
    vB = Vdc_V if hiB[k] else (0.0 if loB[k] else Vdc_V * (-iL < 0))
    v = vA - vB; V[k] = v
    iL += (v - vc) / L_H * dt_s
    vc += (iL - vc / R_ohm) / C_F * dt_s
    VC[k] = vc; IL[k] = iL

# Último ciclo (régimen permanente)
m = t_s >= (ncy - 1) / f1_Hz
t_ms = (t_s[m] - t_s[m][0]) * 1e3
vp, vo, il = V[m], VC[m], IL[m]
assert vo.shape == (400000,)
vavg = uniform_filter1d(vp, int(round(1 / fsw_Hz / dt_s)), mode='nearest')  # promedio en un periodo

# Fundamental y THD: FFT de un ciclo exacto, ventana rectangular, sin relleno de ceros, un lado
Y = np.abs(np.fft.rfft(vo)) / vo.size * 2
V1 = Y[1]; THD = np.sqrt(np.sum(Y[2:1000] ** 2)) / V1 * 100
print('V1 = %.1f VRMS, THD = %.2f %%' % (V1 / np.sqrt(2), THD))

# Gráfica
t0, t1 = 3.92, 4.42                         # ventana del detalle, centrada en el pico
f, (a1, a2, a3) = plt.subplots(3, 1, figsize=(6.8, 6.2), gridspec_kw={'height_ratios': [1, 1, 1.1]})
a1.axvspan(t0, t1, color='#f5d6d0', lw=0)
a1.plot(t_ms, vp, color='#c8c8c8', lw=0.2, label='Pulsos')
a1.plot(t_ms, vavg, color=AZUL, lw=1.4, label='Promedio en un periodo de conmutación')
a1.set_ylabel('$v_{PWM}$ (V)'); a1.set_ylim(-240, 240); a1.set_xlim(0, t_ms[-1])
a1.set_title('(a) Ciclo completo: pulsos y promedio por periodo de conmutación', fontsize=9.5)
a1.legend(loc='upper right', fontsize=8, framealpha=0.95)

z = (t_ms >= t0) & (t_ms <= t1)
a2.plot(t_ms[z], vp[z], color='#333333', lw=0.8)
a2.plot(t_ms[z], ma * Vdc_V * np.sin(2 * np.pi * f1_Hz * t_ms[z] / 1e3), '--', color=ROJO, lw=1.2,
        label='$m_a\\,V_{dc}\\,\\mathrm{sen}(\\omega t)$')
a2.set_xlim(t0, t1); a2.set_ylim(-40, 240); a2.set_ylabel('$v_{PWM}$ (V)')
a2.set_title('(b) Detalle de 0,5 ms cerca del pico', fontsize=9.5)
a2.legend(loc='lower right', fontsize=8, framealpha=0.95)

a3.plot(t_ms, vo, color=AZUL, lw=1.4, label='$v_o$')
a3.set_ylim(-200, 200); a3.set_xlim(0, t_ms[-1]); a3.set_ylabel('$v_o$ (V)', color=AZUL)
a3.set_xlabel('Tiempo (ms)')
b3 = a3.twinx(); b3.grid(False)
b3.plot(t_ms, il, color=ROJO, lw=0.4, alpha=0.45, label='$i_L$')
b3.set_ylim(-10, 10); b3.set_ylabel('$i_L$ (A)', color=ROJO)
a3.set_zorder(b3.get_zorder() + 1); a3.patch.set_visible(False)
a3.set_title('(c) Voltaje de salida y corriente en la inductancia', fontsize=9.5)
a3.text(0.25, -170, ('$V_{1,RMS}$ = %.1f V,  THD = %.2f %%' % (V1 / np.sqrt(2), THD)).replace('.', ','),
        fontsize=9, bbox=dict(fc='white', ec='#cccccc'))
for a in (a1, a2, a3):
    a.xaxis.set_major_formatter(coma)
f.tight_layout(h_pad=0.6)

aqui = os.path.dirname(os.path.abspath(__file__))
salida = os.path.join(aqui, '..', '..', 'Figuras Tesis')
os.makedirs(salida, exist_ok=True)
f.savefig(os.path.join(salida, 'Figura_21_simulacion_canal1.png'), dpi=300, bbox_inches='tight',
          pad_inches=0.06, facecolor='white')
