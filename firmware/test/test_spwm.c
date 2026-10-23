/*
 * test_spwm.c
 * Verifica la modulación tal como la calcula la ISR: se simulan los
 * comparadores periodo a periodo y se obtiene la fundamental con una DFT de
 * un periodo exacto (sin ventana, sin relleno de ceros, un solo lado).
 *   make -C firmware/test
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "spwm.h"
#include "config.h"

static int n_ok;
#define OK(cond) do { if (!(cond)) { printf("FALLA: %s (línea %d)\n", #cond, __LINE__); return 1; } n_ok++; } while (0)
#define CERCA(a, b, tol) (fabs((double)(a) - (double)(b)) <= (tol))

/* Amplitud de la fundamental de x[k] sobre N muestras que cubren un número
 * entero de periodos de la fundamental (ciclos ciclos). */
static double amplitud(const double *x, int N, int ciclos)
{
    double re = 0, im = 0;
    for (int k = 0; k < N; k++) {
        double w = 2.0 * M_PI * ciclos * k / N;
        re += x[k] * cos(w);
        im -= x[k] * sin(w);
    }
    return 2.0 * sqrt(re * re + im * im) / N;
}

int main(void)
{
    spwm_iniciar_tabla();

    /* Tabla */
    OK(spwm_seno_q15(0) == 0);
    OK(spwm_seno_q15(1u << 30) == SPWM_Q15_UNO);            /* 90° */
    OK(spwm_seno_q15(3u << 30) == -SPWM_Q15_UNO);           /* 270° */

    /* Periodos del MCPWM (sección 4.3.8) */
    OK(spwm_pico_desde_fsw(MCPWM_RESOLUCION_HZ, 20000) == 2000);
    OK(spwm_pico_desde_fsw(MCPWM_RESOLUCION_HZ, 10000) == 4000);
    OK(spwm_pico_desde_fsw(MCPWM_RESOLUCION_HZ, 25200) == 1587);
    OK(TM_CH1_CUENTAS * 1e9 / MCPWM_RESOLUCION_HZ == 800);
    OK(TM_CH2_CUENTAS * 1e9 / MCPWM_RESOLUCION_HZ == 600);

    /* Ciclo de trabajo y límite del bootstrap */
    spwm_ref_t r = { .pico = 2000, .m_q15 = 0, .cmp_max = spwm_cmp_max(2000, CICLO_MAX_PERMIL) };
    OK(r.cmp_max == 1900);
    OK(spwm_cmp(&r, SPWM_Q15_UNO) == 1000);                 /* m = 0: 50 % */
    r.m_q15 = SPWM_Q15_UNO;
    OK(spwm_cmp(&r, SPWM_Q15_UNO) == 1900);                 /* recortado a 95 % */
    OK(spwm_cmp(&r, -SPWM_Q15_UNO) == 1);                   /* mínimo 1 cuenta */

    /* Ley V/f y ecuación 23 */
    OK(CERCA(vf_ma(10, CH2_MA0, 1200), 0.05 + 0.95 * 10 / 1200.0, 1e-6));
    OK(CERCA(vf_ma(1200, CH2_MA0, 1200), 1.0, 1e-6));
    OK(CERCA(vf_ma(600, CH2_MA0, 1200), 0.525, 1e-6));
    OK(vf_fsw(300, 10000, 21, 25200) == 10000);
    OK(vf_fsw(600, 10000, 21, 25200) == 12600);
    OK(vf_fsw(1200, 10000, 21, 25200) == 25200);
    OK(vf_fsw(1300, 10000, 21, 25200) == 25200);
    OK(vf_fsw(900, 20000, 21, 25200) == 20000);
    OK(spwm_dtheta(1, 21) == 204522252u);                   /* 360°/21 */
    OK(CERCA(rampa_paso(10, 600, 0.1f), 10.1, 1e-5));
    OK(CERCA(rampa_paso(600, 10, 0.1f), 599.9, 1e-3));
    OK(rampa_paso(599.95f, 600, 0.1f) == 600.0f);

    /* ---- Canal 1: puente H, SPWM unipolar, ma = 0,88, bus 200 V ----
     * Valor medio del voltaje de salida en cada periodo de conmutación:
     * v = Vdc·(dA − dB). Sin tiempo muerto la fundamental debe ser ma·Vdc. */
    {
        const double vdc = 200.0;
        uint32_t pico = spwm_pico_desde_fsw(MCPWM_RESOLUCION_HZ, CH1_FSW_HZ);
        float fsw = spwm_fsw_real(MCPWM_RESOLUCION_HZ, pico);
        spwm_ref_t c1 = { .pico = pico, .dtheta = spwm_dtheta(CH1_F1_HZ, fsw),
                          .m_q15 = spwm_q15(CH1_MA_FINAL), .cmp_max = spwm_cmp_max(pico, CICLO_MAX_PERMIL) };
        const int ciclos = 3;
        const int N = (int)lround(ciclos * fsw / CH1_F1_HZ);   /* 1000 periodos */
        static double v[2000];
        uint32_t th = 0;
        double dmax = 0;
        for (int k = 0; k < N; k++) {
            th += c1.dtheta;
            int32_t s = spwm_seno_q15(th);
            double da = spwm_cmp(&c1, s) / (double)pico;
            double db = spwm_cmp(&c1, -s) / (double)pico;
            if (da > dmax) dmax = da;
            v[k] = vdc * (da - db);
        }
        double vrms = amplitud(v, N, ciclos) / sqrt(2.0);
        printf("CH1: fundamental %.2f VRMS (esperado %.2f), ciclo máximo %.3f\n",
               vrms, CH1_MA_FINAL * vdc / sqrt(2.0), dmax);
        OK(CERCA(vrms, CH1_MA_FINAL * vdc / sqrt(2.0), 0.3));
        OK(dmax <= 0.95 + 1e-9);
        /* Con la caída por tiempo muerto de la ecuación 24 (≈ 5,8 VRMS en
         * las dos ramas) el voltaje queda cerca de 118,9 VRMS del documento */
        double caida = (4.0 / M_PI) * 2.0 * vdc * 800e-9 * CH1_FSW_HZ / sqrt(2.0);
        printf("CH1: con caída por tiempo muerto ≈ %.1f VRMS\n", vrms - caida);
    }

    /* ---- Canal 2: trifásico, voltaje de línea y simetría ---- */
    {
        const double vdc = 12.0;
        const float f1s[3] = { 300.0f, 600.0f, 1200.0f };
        for (int j = 0; j < 3; j++) {
            float f1 = f1s[j];
            float ma = vf_ma(f1, CH2_MA0, CH2_F1_MAX_HZ);
            float fsw = vf_fsw(f1, CH2_FSW_BASE_DEFECTO, CH2_MF_SINCRONO, CH2_FSW_MAX_HZ);
            uint32_t pico = spwm_pico_desde_fsw(MCPWM_RESOLUCION_HZ, fsw);
            float fr = spwm_fsw_real(MCPWM_RESOLUCION_HZ, pico);
            bool sinc = CH2_MF_SINCRONO * f1 >= CH2_FSW_BASE_DEFECTO;
            spwm_ref_t c2 = { .pico = pico, .dtheta = sinc ? 204522252u : spwm_dtheta(f1, fr),
                              .m_q15 = spwm_q15(ma), .cmp_max = spwm_cmp_max(pico, CICLO_MAX_PERMIL) };
            const int ciclos = 5;
            const int N = sinc ? (int)(ciclos * CH2_MF_SINCRONO) : (int)lround(ciclos * fr / f1);
            static double vuv[4000], vvw[4000];
            uint32_t th = 0;
            for (int k = 0; k < N; k++) {
                th += c2.dtheta;
                double du = spwm_cmp(&c2, spwm_seno_q15(th)) / (double)pico;
                double dv = spwm_cmp(&c2, spwm_seno_q15(th - 1431655765u)) / (double)pico;
                double dw = spwm_cmp(&c2, spwm_seno_q15(th + 1431655765u)) / (double)pico;
                vuv[k] = vdc * (du - dv);
                vvw[k] = vdc * (dv - dw);
            }
            double auv = amplitud(vuv, N, ciclos), avw = amplitud(vvw, N, ciclos);
            double esperado = sqrt(3.0) / 2.0 * ma * vdc;
            printf("CH2 f1=%4.0f Hz: fsw=%5.0f Hz %s, ma=%.3f, VLL1 pico UV=%.2f VW=%.2f V (esperado %.2f)\n",
                   f1, fr, sinc ? "síncrona " : "asíncrona", ma, auv, avw, esperado);
            OK(CERCA(auv, avw, 0.02 * esperado + 0.01));     /* fases simétricas */
            /* A 1200 Hz el recorte del 95 % baja un poco la fundamental */
            OK(auv <= esperado * 1.01 && auv >= esperado * (ma > 0.9 ? 0.93 : 0.98));
        }
    }

    printf("test_spwm: %d comprobaciones correctas\n", n_ok);
    return 0;
}
