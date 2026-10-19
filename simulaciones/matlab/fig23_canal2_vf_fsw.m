% Figura 23. Canal 2: (a) ley V/f y (b) frecuencia de conmutación contra la frecuencia fundamental.
% Regla de conmutación: fsw = max(fbase, 21*f1), con mf = 21 (impar y múltiplo de 3) en el modo síncrono.
clear; close all;
f1 = linspace(10, 1200, 1191); fmax = 1200;
ma0 = 0.05;                                   % incremento ilustrativo a baja frecuencia (por definir)
ma = f1 / fmax; mab = ma0 + (1 - ma0) * f1 / fmax;
azul = [0.12 0.37 0.66]; rojo = [0.75 0.22 0.17];
f = figure('Units', 'centimeters', 'Position', [2 2 17 14], 'Color', 'w');
tl = tiledlayout(f, 2, 1, 'TileSpacing', 'compact', 'Padding', 'compact');
a1 = nexttile(tl);
yyaxis(a1, 'left');
p1 = plot(a1, f1, ma, '--', 'Color', [0.35 0.35 0.35], 'LineWidth', 1.2); hold(a1, 'on');
p2 = plot(a1, f1, mab, '-', 'Color', azul, 'LineWidth', 1.8);
ylim(a1, [0 1.1]); ylabel(a1, 'm_a'); a1.YAxis(1).Color = 'k';
yyaxis(a1, 'right'); ylim(a1, [0 1.1 * 0.612 * 12]); ylabel(a1, 'V_{LL1,RMS} con bus de 12 V (V)'); a1.YAxis(2).Color = 'k';
xlim(a1, [0 1250]); grid(a1, 'on'); box(a1, 'on');
title(a1, '(a) Ley V/f', 'FontWeight', 'normal');
legend(a1, [p1 p2], {'m_a = f_1 / 1,2 kHz (V/f constante)', 'Con incremento a baja frecuencia (valor por definir)'}, 'Location', 'northwest');
a2 = nexttile(tl); hold(a2, 'on');
fb = [10e3 20e3]; col = {azul, rojo}; h = gobjects(1, 2);
for k = 1:2
    fsw = max(fb(k), 21 * f1);
    h(k) = plot(a2, f1, fsw / 1e3, 'Color', col{k}, 'LineWidth', 1.8);
    fx = fb(k) / 21; plot(a2, fx, fb(k) / 1e3, 'o', 'Color', col{k}, 'MarkerFaceColor', col{k}, 'MarkerSize', 4);
    text(a2, fx - 90, fb(k) / 1e3 + 1.2, sprintf('%.0f Hz', fx), 'Color', col{k}, 'FontName', 'Times New Roman', 'FontSize', 9);
end
yline(a2, 25.2, ':', 'Color', [0.55 0.55 0.55]);
text(a2, 30, 26.3, '25,2 kHz a 1,2 kHz (m_f = 21)', 'Color', [0.35 0.35 0.35], 'FontName', 'Times New Roman', 'FontSize', 9);
xlim(a2, [0 1250]); ylim(a2, [0 30]); grid(a2, 'on'); box(a2, 'on');
xlabel(a2, 'Frecuencia fundamental f_1 (Hz)'); ylabel(a2, 'f_{sw} (kHz)');
title(a2, '(b) Frecuencia de conmutación: f_{sw} = máx(f_{base}, 21 f_1)', 'FontWeight', 'normal');
legend(a2, h, {'Base de 10 kHz', 'Base de 20 kHz'}, 'Location', 'southeast');
set([a1 a2], 'FontName', 'Times New Roman', 'FontSize', 10);
yyaxis(a1, 'left'); coma(a1);
exportgraphics(f, fullfile(carpeta_figuras, 'Figura_23_canal2_vf_fsw.png'), 'Resolution', 300);
