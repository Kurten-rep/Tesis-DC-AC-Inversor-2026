% Figura 23. Índice de modulación y voltaje de línea contra frecuencia en el Canal 2 (ley V/f).
clear; close all;
fr = linspace(10, 1200, 600); ma = fr / 1200;
f = figure('Units', 'centimeters', 'Position', [2 2 16 8.5], 'Color', 'w');
patch([10e3/9 1200 1200 10e3/9], [0 0 1.1 1.1], [0.96 0.84 0.82], 'EdgeColor', 'none'); hold on;
p1 = plot(fr, ma, 'Color', [0.12 0.37 0.66], 'LineWidth', 2);
p2 = plot([10 120], [0.06 0.10], '--', 'Color', [0.75 0.22 0.17], 'LineWidth', 1.4);
text(1150, 0.35, {'m_f < 9', 'con f_{sw} = 10 kHz'}, 'HorizontalAlignment', 'right', 'FontName', 'Times New Roman');
xlim([0 1250]); ylim([0 1.1]); grid on; box on;
xlabel('Frecuencia fundamental (Hz)'); ylabel('m_a');
yyaxis right; ylim([0 1.1 * 7.35]); ylabel('V_{LL1,RMS} con bus de 12 V (V)');
legend([p1 p2], {'m_a = f / 1,2 kHz (V/f constante)', 'Incremento a baja frecuencia (por definir)'}, 'Location', 'northwest');
set(gca, 'FontName', 'Times New Roman', 'FontSize', 11);
exportgraphics(f, fullfile(carpeta_figuras, 'Figura_23_ma_vs_frecuencia.png'), 'Resolution', 300);
