% Figura 9. Ley V/f normalizada con y sin incremento de voltaje a baja frecuencia.
clear; close all;
x = linspace(0, 1.2, 600);              % f / f_nom
ideal = min(x, 1);                        % V/f constante
boost = 0.08;                             % incremento ilustrativo (por definir)
vb = min(boost + (1 - boost) * x, 1);
f = figure('Units', 'centimeters', 'Position', [2 2 16 8.5], 'Color', 'w');
area([0 0.15], [1.15 1.15], 'FaceColor', [0.93 0.93 0.93], 'EdgeColor', 'none'); hold on;
p1 = plot(x, ideal, '--', 'Color', [0.35 0.35 0.35], 'LineWidth', 1.2);
p2 = plot(x, vb, '-', 'Color', [0.12 0.37 0.66], 'LineWidth', 2);
xline(1, '-', 'Color', [0.4 0.4 0.4]);
text(0.075, 0.8, {'Zona de', 'incremento'}, 'HorizontalAlignment', 'center', 'FontName', 'Times New Roman', 'FontSize', 10);
text(1.02, 0.4, {'Frecuencia', 'nominal'}, 'FontName', 'Times New Roman', 'FontSize', 10);
grid on; box on; xlim([0 1.2]); ylim([0 1.15]);
xlabel('f / f_{nom}'); ylabel('V / V_{nom}');
legend([p1 p2], {'V/f constante', 'Con incremento a baja frecuencia'}, 'Location', 'southeast');
set(gca, 'FontName', 'Times New Roman', 'FontSize', 11);
coma(gca);
exportgraphics(f, fullfile(carpeta_figuras, 'Figura_09_ley_vf.png'), 'Resolution', 300);
