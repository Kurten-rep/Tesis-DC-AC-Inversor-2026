/*
 * spwm.c
 * Tabla de seno, cálculo de ciclos de trabajo, ley V/f y regla de la
 * frecuencia de conmutación (ecuación 23).
 */
#include "spwm.h"
#include <math.h>

/* En RAM interna (no const) para que la ISR la lea aunque la caché de la
 * flash esté deshabilitada. */
#ifdef ESP_PLATFORM
static DRAM_ATTR int16_t s_seno[SPWM_LUT_LEN];
#else
static int16_t s_seno[SPWM_LUT_LEN];
#endif

void spwm_iniciar_tabla(void)
{
    for (uint32_t k = 0; k < SPWM_LUT_LEN; k++) {
        double v = sin(2.0 * M_PI * (double)k / (double)SPWM_LUT_LEN);
        s_seno[k] = (int16_t)lround(v * SPWM_Q15_UNO);
    }
}

SPWM_IRAM int32_t spwm_seno_q15(uint32_t theta)
{
    return s_seno[theta >> (32 - SPWM_LUT_BITS)];
}

/* cmp = pico · (1 + m·s) / 2, limitado a [1, cmp_max].
 * m·s >> 15 queda en ±32767, así que 32768 + (m·s >> 15) está en [1, 65535]
 * y pico · 65535 cabe en 32 bits mientras pico < 65536. */
SPWM_IRAM uint32_t spwm_cmp(const spwm_ref_t *r, int32_t s_q15)
{
    int32_t ms = (r->m_q15 * s_q15) >> 15;
    uint32_t cmp = (r->pico * (uint32_t)(32768 + ms)) >> 16;
    if (cmp < 1) {
        cmp = 1;
    }
    if (cmp > r->cmp_max) {
        cmp = r->cmp_max;
    }
    return cmp;
}

uint32_t spwm_pico_desde_fsw(uint32_t resolucion_hz, float fsw_hz)
{
    /* P = resolución / (2·fsw), redondeado */
    return (uint32_t)lroundf((float)resolucion_hz / (2.0f * fsw_hz));
}

float spwm_fsw_real(uint32_t resolucion_hz, uint32_t pico)
{
    return (float)resolucion_hz / (2.0f * (float)pico);
}

uint32_t spwm_dtheta(float f1_hz, float fsw_hz)
{
    double d = (double)f1_hz / (double)fsw_hz * 4294967296.0;
    if (d >= 4294967295.0) {
        d = 4294967295.0;
    }
    return (uint32_t)llround(d);
}

int32_t spwm_q15(float x)
{
    if (x < 0.0f) {
        x = 0.0f;
    }
    if (x > 1.0f) {
        x = 1.0f;
    }
    return (int32_t)lroundf(x * SPWM_Q15_UNO);
}

/* Ley V/f con incremento lineal: ma = ma0 + (1 - ma0)·f1/f1_max */
float vf_ma(float f1_hz, float ma0, float f1_max_hz)
{
    float ma = ma0 + (1.0f - ma0) * f1_hz / f1_max_hz;
    if (ma < 0.0f) {
        ma = 0.0f;
    }
    if (ma > 1.0f) {
        ma = 1.0f;
    }
    return ma;
}

/* Ecuación 23: fsw = máx(fbase, 21·f1), con tope en fsw_max */
float vf_fsw(float f1_hz, float fsw_base_hz, uint32_t mf, float fsw_max_hz)
{
    float fsw = (float)mf * f1_hz;
    if (fsw < fsw_base_hz) {
        fsw = fsw_base_hz;
    }
    if (fsw > fsw_max_hz) {
        fsw = fsw_max_hz;
    }
    return fsw;
}

float rampa_paso(float actual, float objetivo, float max_paso)
{
    float d = objetivo - actual;
    if (d > max_paso) {
        d = max_paso;
    } else if (d < -max_paso) {
        d = -max_paso;
    }
    return actual + d;
}

uint32_t spwm_cmp_max(uint32_t pico, uint32_t permil)
{
    return (pico * permil) / 1000u;
}
