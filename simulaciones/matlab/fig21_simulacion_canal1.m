% Figura 21. Simulación del Canal 1: SPWM unipolar a 20 kHz, filtro LC y carga resistiva.
% Modelo de interruptores ideales. Con td > 0 se incluye el tiempo muerto: durante td
% la rama queda abierta y su voltaje lo fija el diodo que conduce según el signo de la corriente.
% (a) ciclo completo con el promedio por periodo de conmutación, (b) detalle de 0,5 ms
% cerca del pico, (c) voltaje de salida y corriente en la inductancia.
clear; close all;
Vdc = 200; ma = 0.88; f1 = 60; fsw = 20e3;   % ma = 0,88 compensa la caída por tiempo muerto
L = 1.15e-3; C = 6.8e-6; R = 22.5;        % carga de 640 VA a 120 VRMS
td = 800e-9;                              % tiempo muerto (poner 0 para el caso ideal)
dt = 1 / f1 / 400000;                     % 400 000 muestras exactas por ciclo
ncy = 5; N = round(ncy / f1 / dt);
t = (0:N-1) * dt;
ref = ma * sin(2 * pi * f1 * t);
tri = 2 * abs(2 * mod(t * fsw, 1) - 1) - 1;
cA = ref > tri; cB = -ref > tri;          % comandos de cada rama (unipolar)
nd = round(td / dt);
dA = [false(1, nd) cA(1:end-nd)]; dB = [false(1, nd) cB(1:end-nd)];
hiA = cA & dA; loA = ~cA & ~dA;           % encendido retrasado td
hiB = cB & dB; loB = ~cB & ~dB;
iL = 0; vc = 0; VC = zeros(1, N); IL = zeros(1, N); V = zeros(1, N);
for k = 1:N
    if hiA(k), vA = Vdc; elseif loA(k), vA = 0; else, vA = Vdc * (iL < 0); end
    if hiB(k), vB = Vdc; elseif loB(k), vB = 0; else, vB = Vdc * (-iL < 0); end
    v = vA - vB; V(k) = v;
    iL = iL + (v - vc) / L * dt;
    vc = vc + (iL - vc / R) / C * dt;
    VC(k) = vc; IL(k) = iL;
end

% Último ciclo (régimen permanente)
m = t >= (ncy - 1) / f1;
tt = (t(m) - t(find(m, 1))) * 1e3;        % ms
vp = V(m); vo = VC(m); il = IL(m);
vavg = movmean(vp, round(1 / fsw / dt));  % promedio en un periodo de conmutación

% Fundamental y THD (FFT de un ciclo exacto, sin ventana, espectro de un lado)
n = numel(vo); Y = abs(fft(vo)) / n * 2;
V1 = Y(2); THD = sqrt(sum(Y(3:1000) .^ 2)) / V1 * 100;
fprintf('V1 = %.1f VRMS, THD = %.2f %%\n', V1 / sqrt(2), THD);

% Gráfica
gris = [0.80 0.80 0.80]; azul = [0.12 0.37 0.66]; rojo = [0.75 0.22 0.17];
t0 = 3.92; t1 = 4.42;                     % ventana del detalle, centrada en el pico
f = figure('Units', 'centimeters', 'Position', [2 2 16 14], 'Color', 'w');
tl = tiledlayout(f, 3, 1, 'TileSpacing', 'compact', 'Padding', 'compact');

a1 = nexttile(tl);
patch(a1, [t0 t1 t1 t0], [-240 -240 240 240], [0.96 0.84 0.82], 'EdgeColor', 'none'); hold(a1, 'on');
h1 = plot(a1, tt, vp, 'Color', gris, 'LineWidth', 0.3);
h2 = plot(a1, tt, vavg, 'Color', azul, 'LineWidth', 1.4);
ylabel(a1, 'v_{PWM} (V)'); ylim(a1, [-240 240]); xlim(a1, [0 tt(end)]); grid(a1, 'on'); box(a1, 'on');
title(a1, '(a) Ciclo completo: pulsos y promedio por periodo de conmutación', 'FontWeight', 'normal');
legend(a1, [h1 h2], {'Pulsos', 'Promedio en un periodo de conmutación'}, 'Location', 'northeast');

a2 = nexttile(tl);
z = tt >= t0 & tt <= t1;
plot(a2, tt(z), vp(z), 'Color', [0.25 0.25 0.25], 'LineWidth', 0.8); hold(a2, 'on');
h3 = plot(a2, tt(z), ma * Vdc * sin(2 * pi * f1 * tt(z) / 1e3), '--', 'Color', rojo, 'LineWidth', 1.2);
xlim(a2, [t0 t1]); ylim(a2, [-40 240]); ylabel(a2, 'v_{PWM} (V)'); grid(a2, 'on'); box(a2, 'on');
title(a2, '(b) Detalle de 0,5 ms cerca del pico', 'FontWeight', 'normal');
legend(a2, h3, {'m_a V_{dc} sen(\omegat)'}, 'Location', 'southeast');

a3 = nexttile(tl);
yyaxis(a3, 'left');
plot(a3, tt, vo, '-', 'Color', azul, 'LineWidth', 1.4); ylim(a3, [-200 200]); ylabel(a3, 'v_o (V)');
yyaxis(a3, 'right');
plot(a3, tt, il, '-', 'Color', [rojo 0.35], 'LineWidth', 0.5); ylim(a3, [-10 10]); ylabel(a3, 'i_L (A)');
a3.YAxis(1).Color = azul; a3.YAxis(2).Color = rojo;
xlim(a3, [0 tt(end)]); xlabel(a3, 'Tiempo (ms)'); grid(a3, 'on'); box(a3, 'on');
title(a3, '(c) Voltaje de salida y corriente en la inductancia', 'FontWeight', 'normal');
txt = sprintf('V_{1,RMS} = %.1f V,  THD = %.2f %%', V1 / sqrt(2), THD);
text(a3, 0.3, -7, strrep(txt, '.', ','), 'BackgroundColor', 'w', 'EdgeColor', [0.8 0.8 0.8], ...
    'FontName', 'Times New Roman', 'FontSize', 10);

set([a1 a2 a3], 'FontName', 'Times New Roman', 'FontSize', 10);
coma(a1); coma(a2);
a3.XTickLabel = strrep(cellstr(num2str(a3.XTick(:))), '.', ',');
exportgraphics(f, fullfile(carpeta_figuras, 'Figura_21_simulacion_canal1.png'), 'Resolution', 300);
