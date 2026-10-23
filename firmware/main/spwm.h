/*
 * spwm.h
 * Matemática de la modulación, sin dependencias del hardware, para poder
 * probarla en el PC (carpeta test/).
 *
 * Convenciones:
 *  - Ángulo: acumulador de 32 bits, 2^32 = 360°.
 *  - Índice de modulación y seno en Q15 (32767 ≈ 1,0).
 *  - Temporizador en modo ascendente/descendente: "pico" = cuentas de 0 al
 *    máximo, periodo total = 2·pico. Ciclo de trabajo del lado alto = cmp/pico.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define SPWM_IRAM IRAM_ATTR
#else
#define SPWM_IRAM
#endif

#define SPWM_LUT_BITS   10
#define SPWM_LUT_LEN    (1u << SPWM_LUT_BITS)
#define SPWM_Q15_UNO    32767

/* Referencia que la tarea de rampa publica y la ISR consume. */
typedef struct {
    uint32_t pico;      /* cuentas hasta el pico del triángulo */
    uint32_t dtheta;    /* avance del ángulo por periodo de conmutación */
    int32_t  m_q15;     /* índice de modulación */
    uint32_t cmp_max;   /* límite del bootstrap */
} spwm_ref_t;

void     spwm_iniciar_tabla(void);
int32_t  spwm_seno_q15(uint32_t theta);
uint32_t spwm_cmp(const spwm_ref_t *r, int32_t s_q15);

/* Funciones de la tarea de rampa (usan coma flotante, nunca en ISR) */
uint32_t spwm_pico_desde_fsw(uint32_t resolucion_hz, float fsw_hz);
float    spwm_fsw_real(uint32_t resolucion_hz, uint32_t pico);
uint32_t spwm_dtheta(float f1_hz, float fsw_hz);
int32_t  spwm_q15(float x);
float    vf_ma(float f1_hz, float ma0, float f1_max_hz);
float    vf_fsw(float f1_hz, float fsw_base_hz, uint32_t mf, float fsw_max_hz);
float    rampa_paso(float actual, float objetivo, float max_paso);
uint32_t spwm_cmp_max(uint32_t pico, uint32_t permil);
