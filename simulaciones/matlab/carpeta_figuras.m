function d = carpeta_figuras()
% Devuelve la carpeta "Figuras Tesis" donde se guardan los PNG.
% Estructura esperada: Tesis DC-AC Inversor 2026/Anexos/Scripts MATLAB/ (este archivo)
%                      Tesis DC-AC Inversor 2026/Figuras Tesis/        (salida)
aqui = fileparts(mfilename('fullpath'));
d = fullfile(aqui, '..', '..', 'Figuras Tesis');
if ~exist(d, 'dir'), mkdir(d); end
end
