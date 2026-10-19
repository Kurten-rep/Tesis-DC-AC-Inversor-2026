% Voltaje fundamental y THD del Canal 1 para distintas frecuencias de conmutación y tiempos muertos
% (datos de la Tabla 12 y del ajuste de ma). Cada caso tarda algunos segundos.
clear; clc;
for R = [22.5 360]
    for fsw = [10e3 20e3 40e3]
        for td = [0 800e-9]
            [V, T] = sim_canal1(0.85, fsw, td, R);
            fprintf('R = %5.1f ohm, fsw = %2.0f kHz, td = %4.0f ns: V1 = %6.2f VRMS, THD = %.2f %%\n', R, fsw / 1e3, td * 1e9, V, T);
        end
    end
end
for ma = [0.87 0.88 0.89]
    for R = [22.5 360]
        [V, T] = sim_canal1(ma, 20e3, 800e-9, R);
        fprintf('ma = %.2f, R = %5.1f ohm: V1 = %6.2f VRMS, THD = %.2f %%\n', ma, R, V, T);
    end
end
