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
 *
 * Q15 significa entero con signo donde 15 bits son la parte fraccionaria:
 * el valor real es entero / 32768. Así la ISR no usa coma flotante.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* SPWM_IRAM marca las funciones que corren dentro de la ISR para que vivan
 * en la RAM de instrucciones del ESP32. En el PC (pruebas) no hace nada. */
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define SPWM_IRAM IRAM_ATTR
#else
#define SPWM_IRAM
#endif

#define SPWM_LUT_BITS   10                      /* bits del índice de la tabla de seno */
#define SPWM_LUT_LEN    (1u << SPWM_LUT_BITS)   /* 1024 puntos por periodo */
#define SPWM_Q15_UNO    32767                   /* 1,0 en Q15 */

/* Referencia que la tarea de rampa publica y la ISR consume. */
typedef struct {
    uint32_t pico;      /* cuentas hasta el pico del triángulo */
    uint32_t dtheta;    /* avance del ángulo por periodo de conmutación */
    int32_t  m_q15;     /* índice de modulación */
    uint32_t cmp_max;   /* límite del bootstrap */
} spwm_ref_t;

/* Llena la tabla de seno. Se llama una vez al arrancar. */
void     spwm_iniciar_tabla(void);
/* Seno del ángulo theta (2^32 = 360°), resultado en Q15 de -32767 a 32767. */
int32_t  spwm_seno_q15(uint32_t theta);
/* Valor de comparación del temporizador para la muestra de seno s_q15. */
uint32_t spwm_cmp(const spwm_ref_t *r, int32_t s_q15);

/* Funciones de la tarea de rampa (usan coma flotante, nunca en ISR) */
/* Cuentas hasta el pico del triángulo para obtener la fsw pedida. */
uint32_t spwm_pico_desde_fsw(uint32_t resolucion_hz, float fsw_hz);
/* Frecuencia de conmutación real que resulta de un pico entero. */
float    spwm_fsw_real(uint32_t resolucion_hz, uint32_t pico);
/* Incremento del acumulador de ángulo por periodo de conmutación. */
uint32_t spwm_dtheta(float f1_hz, float fsw_hz);
/* Convierte un número de 0.0 a 1.0 a Q15, saturando fuera de rango. */
int32_t  spwm_q15(float x);
/* Ley V/f: índice de modulación en función de la frecuencia. */
float    vf_ma(float f1_hz, float ma0, float f1_max_hz);
/* Regla de la frecuencia de conmutación (ecuación 23). */
float    vf_fsw(float f1_hz, float fsw_base_hz, uint32_t mf, float fsw_max_hz);
/* Avanza "actual" hacia "objetivo" sin pasar de max_paso. */
float    rampa_paso(float actual, float objetivo, float max_paso);
/* Límite de comparación según el ciclo máximo, en milésimas. */
uint32_t spwm_cmp_max(uint32_t pico, uint32_t permil);
