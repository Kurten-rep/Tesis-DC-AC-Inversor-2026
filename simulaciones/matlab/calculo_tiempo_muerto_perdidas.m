% Tiempo muerto mínimo y pérdidas por transistor de los dos canales (Tablas 12, 13 y 14).
% Modelo de carga de compuerta (AN-944): la compuerta se carga y descarga a través de
% Rg + Rdrv, con la meseta de Miller a voltaje constante. Driver IR2110 con VCC = 15 V.
clear; clc;
VCC = 15; Rg = 10; Rdrv_typ = 15 / 2.5; Rdrv_max = 15 / 2.0;   % IO = 2,5 A típ., 2,0 A mín.
MT = 10e-9;                                                   % desfase máximo entre salidas del IR2110
fsw = [10e3 20e3 40e3];
% Datos:      Qg_ref  V_ref Qgs    Qgd    Vth_min Vth_typ Vpl  Rds   kT   Qrr     IF_qrr Coss_er  Vdc Ipk   Irms
d(1) = struct('n','IRFP360',   'Qref',210e-9,'Vref',10,'Qgs',30e-9,'Qgd',110e-9,'Vmin',2.0,'Vtyp',3.0, ...
              'Vpl',5.5,'Rds',0.20,'kT',2.0,'Qrr',5.6e-6,'IFq',23,'Coss',300e-12,'Vdc',200,'Ipk',7.54,'Irms',5.33);
d(2) = struct('n','IRLZ44A-VB','Qref',66e-9,'Vref',5,'Qgs',12e-9,'Qgd',43e-9,'Vmin',1.0,'Vtyp',1.75, ...
              'Vpl',3.0,'Rds',0.024,'kT',1.7,'Qrr',0.84e-6,'IFq',51,'Coss',600e-12,'Vdc',12,'Ipk',14.14,'Irms',10.0);
for k = 1:2
    m = d(k);
    s = (m.Qref - m.Qgs - m.Qgd) / (m.Vref - m.Vpl);   % pendiente de carga sobre la meseta (F)
    Q15 = m.Qref + s * (VCC - m.Vref);                  % carga total a 15 V
    Cin = m.Qgs / m.Vpl;                                % capacitancia equivalente bajo la meseta
    % Apagado más lento (Rdrv máx., Vth mín.) y encendido más rápido (Rdrv típ., Vth mín.)
    R = Rg + Rdrv_max;
    t1 = R * s * log(VCC / m.Vpl); t2 = m.Qgd / (m.Vpl / R); t3 = R * Cin * log(m.Vpl / m.Vmin);
    toff = t1 + t2 + t3;
    ton_d = (Rg + Rdrv_typ) * Cin * log(VCC / (VCC - m.Vmin));
    tdmin = toff - ton_d + MT;
    % Tiempos típicos de transición para las pérdidas
    R = Rg + Rdrv_typ;
    tvr = m.Qgd / (m.Vpl / R); tfi = R * Cin * log(m.Vpl / m.Vtyp);
    tri = R * Cin * log((VCC - m.Vtyp) / (VCC - m.Vpl)); tvf = m.Qgd / ((VCC - m.Vpl) / R);
    tsw = tri + tvf + tvr + tfi;
    fprintf('\n%s: Qg(15 V) = %.0f nC\n', m.n, Q15 * 1e9);
    fprintf('  apagado: %.0f + %.0f + %.0f = %.0f ns; retardo de encendido %.1f ns\n', ...
            t1 * 1e9, t2 * 1e9, t3 * 1e9, toff * 1e9, ton_d * 1e9);
    fprintf('  tiempo muerto mínimo = %.0f ns; transiciones típicas = %.0f ns\n', tdmin * 1e9, tsw * 1e9);
    Idev = m.Irms / sqrt(2); Pc = Idev ^ 2 * m.Rds * m.kT;
    for f = fsw
        Psw = 0.5 * m.Vdc * tsw * f * m.Ipk / pi;
        Prr = m.Vdc * f * (m.Qrr * m.Ipk / m.IFq) / pi;
        Poss = 0.5 * m.Coss * m.Vdc ^ 2 * f / 2;
        fprintf('  %2.0f kHz: Pcond %.2f W, Psw %.2f W, Prr %.2f W, Poss %.3f W, total %.2f W\n', ...
                f / 1e3, Pc, Psw, Prr, Poss, Pc + Psw + Prr + Poss);
    end
end
