function [V1rms, THD] = sim_canal1(ma, fsw, td, R)
% Simula el Canal 1 (SPWM unipolar, tiempo muerto, filtro LC, carga R) y devuelve el
% valor eficaz de la fundamental y el THD del voltaje de salida en el último ciclo.
Vdc = 200; f1 = 60; L = 1.15e-3; C = 6.8e-6;
Ncy = 400000; dt = 1 / f1 / Ncy; ncy = 6; N = Ncy * ncy;
t = (0:N-1) * dt;
ref = ma * sin(2 * pi * f1 * t); tri = 2 * abs(2 * mod(t * fsw, 1) - 1) - 1;
cA = ref > tri; cB = -ref > tri; nd = round(td / dt);
dA = [false(1, nd) cA(1:end-nd)]; dB = [false(1, nd) cB(1:end-nd)];
hiA = cA & dA; loA = ~cA & ~dA; hiB = cB & dB; loB = ~cB & ~dB;
iL = 0; vc = 0; vo = zeros(1, Ncy);
for k = 1:N
    if hiA(k), vA = Vdc; elseif loA(k), vA = 0; else, vA = Vdc * (iL < 0); end
    if hiB(k), vB = Vdc; elseif loB(k), vB = 0; else, vB = Vdc * (-iL < 0); end
    iL = iL + (vA - vB - vc) / L * dt; vc = vc + (iL - vc / R) / C * dt;
    if k > N - Ncy, vo(k - N + Ncy) = vc; end
end
Y = abs(fft(vo)) / Ncy * 2;                 % un ciclo exacto, ventana rectangular, un lado
V1rms = Y(2) / sqrt(2); THD = sqrt(sum(Y(3:1000) .^ 2)) / Y(2) * 100;
end
