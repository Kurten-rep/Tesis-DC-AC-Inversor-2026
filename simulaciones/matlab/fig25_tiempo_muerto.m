% Figura 25. Señales HO y LO de una rama a 20 kHz con tiempo muerto de 800 ns.
clear; close all;
T = 50; td = 0.8;                          % us
t = linspace(0, 100, 40001); ph = mod(t, T);
hi = (ph > td) & (ph < 25); lo = (ph > 25 + td) & (ph < T);
f = figure('Units', 'centimeters', 'Position', [2 2 16 6.5], 'Color', 'w');
a1 = subplot(1, 3, [1 2]);
patch([23 28 28 23], [-0.3 -0.3 2.9 2.9], [0.93 0.93 0.93], 'EdgeColor', 'none'); hold on;
plot(t, hi + 1.4, 'Color', [0.12 0.37 0.66], 'LineWidth', 1.3); plot(t, lo, 'Color', [0.75 0.22 0.17], 'LineWidth', 1.3);
yticks([0.5 1.9]); yticklabels({'LO', 'HO'}); ylim([-0.3 2.9]); xlabel('Tiempo (\mus)'); title('(a) Dos periodos a 20 kHz'); grid on;
a2 = subplot(1, 3, 3);
tz = linspace(23, 28, 5001); pz = mod(tz, T);
hz = (pz > td) & (pz < 25); lz = (pz > 25 + td) & (pz < T);
patch([25 25.8 25.8 25], [-0.3 -0.3 2.9 2.9], [0.96 0.84 0.82], 'EdgeColor', 'none'); hold on;
plot(tz, hz + 1.4, 'Color', [0.12 0.37 0.66], 'LineWidth', 1.5); plot(tz, lz, 'Color', [0.75 0.22 0.17], 'LineWidth', 1.5);
text(25.4, 2.65, '800 ns', 'HorizontalAlignment', 'center', 'FontName', 'Times New Roman');
yticks([0.5 1.9]); yticklabels({'LO', 'HO'}); ylim([-0.3 2.9]); xlabel('Tiempo (\mus)'); title('(b) Detalle'); grid on;
set([a1 a2], 'FontName', 'Times New Roman', 'FontSize', 10);
exportgraphics(f, fullfile(carpeta_figuras, 'Figura_25_tiempo_muerto_800ns.png'), 'Resolution', 300);
