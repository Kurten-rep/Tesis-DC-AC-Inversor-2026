/*
 * spwm.c
 * Tabla de seno, cálculo de ciclos de trabajo, ley V/f y regla de la
 * frecuencia de conmutación (ecuación 23).
 *
 * Idea general: la ISR del MCPWM corre una vez por periodo de conmutación.
 * En cada llamada avanza el ángulo, busca el seno en la tabla y lo convierte
 * en un valor de comparación del temporizador. Todo lo que usa coma
 * flotante (frecuencias, ley V/f) se calcula fuera de la ISR, en la tarea
 * de rampa, y llega a la ISR ya convertido a enteros.
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

/* Llena la tabla con un periodo completo de seno en Q15. La posición k
 * guarda sin(2π·k/1024) multiplicado por 32767 y redondeado. */
void spwm_iniciar_tabla(void)
{
    for (uint32_t k = 0; k < SPWM_LUT_LEN; k++) {
        double v = sin(2.0 * M_PI * (double)k / (double)SPWM_LUT_LEN);
        s_seno[k] = (int16_t)lround(v * SPWM_Q15_UNO);
    }
}

/* Devuelve el seno del ángulo theta. Se usan solo los 10 bits más altos del
 * acumulador de 32 bits como índice: desplazar 22 posiciones a la derecha
 * descarta la parte fina del ángulo. */
SPWM_IRAM int32_t spwm_seno_q15(uint32_t theta)
{
    return s_seno[theta >> (32 - SPWM_LUT_BITS)];
}

/* cmp = pico · (1 + m·s) / 2, limitado a [1, cmp_max].
 * m·s >> 15 queda en ±32767, así que 32768 + (m·s >> 15) está en [1, 65535]
 * y pico · 65535 cabe en 32 bits mientras pico < 65536. */
SPWM_IRAM uint32_t spwm_cmp(const spwm_ref_t *r, int32_t s_q15)
{
    /* Producto índice de modulación por seno, devuelto a escala Q15 */
    int32_t ms = (r->m_q15 * s_q15) >> 15;
    /* Se centra en la mitad del triángulo (32768) y se escala al pico;
     * el desplazamiento de 16 bits equivale a dividir entre 65536 */
    uint32_t cmp = (r->pico * (uint32_t)(32768 + ms)) >> 16;
    /* Piso de 1 cuenta: nunca se pide un ciclo exactamente cero */
    if (cmp < 1) {
        cmp = 1;
    }
    /* Techo del bootstrap: nunca se llega al 100 % del lado alto */
    if (cmp > r->cmp_max) {
        cmp = r->cmp_max;
    }
    return cmp;
}

uint32_t spwm_pico_desde_fsw(uint32_t resolucion_hz, float fsw_hz)
{
    /* P = resolución / (2·fsw), redondeado. El 2 aparece porque el
     * temporizador sube hasta el pico y vuelve a bajar en cada periodo. */
    return (uint32_t)lroundf((float)resolucion_hz / (2.0f * fsw_hz));
}

/* Operación inversa: como el pico es un entero, la frecuencia real puede
 * diferir un poco de la pedida; esta función da el valor que realmente sale. */
float spwm_fsw_real(uint32_t resolucion_hz, uint32_t pico)
{
    return (float)resolucion_hz / (2.0f * (float)pico);
}

/* Incremento del ángulo por cada periodo de conmutación:
 * dtheta = (f1 / fsw) · 2^32. Se calcula en doble precisión para no perder
 * resolución y se satura justo bajo 2^32 para no desbordar el uint32_t. */
uint32_t spwm_dtheta(float f1_hz, float fsw_hz)
{
    double d = (double)f1_hz / (double)fsw_hz * 4294967296.0;
    if (d >= 4294967295.0) {
        d = 4294967295.0;
    }
    return (uint32_t)llround(d);
}

/* Convierte un número real entre 0 y 1 al formato Q15, recortando los
 * valores que se salgan del rango. */
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

/* Ley V/f con incremento lineal: ma = ma0 + (1 - ma0)·f1/f1_max
 * A f1 = 0 da ma0 (tensión mínima para vencer la caída resistiva del motor)
 * y a f1 = f1_max da 1,0. El resultado se limita a [0, 1]. */
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

/* Ecuación 23: fsw = máx(fbase, 21·f1), con tope en fsw_max
 * A frecuencias bajas se usa la base fija; al subir f1 la conmutación pasa
 * a ser síncrona (mf veces f1) para mantener estable la forma de onda. */
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

/* Mueve "actual" hacia "objetivo" como máximo max_paso en cada llamada.
 * Se usa para las rampas de frecuencia y de índice de modulación. */
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

/* Valor máximo de comparación: pico · permil / 1000 (950 = 95 %). */
uint32_t spwm_cmp_max(uint32_t pico, uint32_t permil)
{
    return (pico * permil) / 1000u;
}
