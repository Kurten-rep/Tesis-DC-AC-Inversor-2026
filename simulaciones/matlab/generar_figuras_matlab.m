% Genera las cuatro figuras de MATLAB de la tesis.
% Los scripts viven en Anexos/Scripts MATLAB y los PNG se guardan en "Figuras Tesis".
% Se puede ejecutar desde cualquier carpeta: agrega esta carpeta al path primero.
addpath(fileparts(mfilename('fullpath')));
fig09_ley_vf; fig21_simulacion_canal1; fig23_canal2_vf_fsw; fig25_tiempo_muerto;
disp(['Listo: Figuras 9, 21, 23 y 25 guardadas en ' carpeta_figuras]);
