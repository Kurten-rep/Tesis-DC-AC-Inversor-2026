/*
 * medicion.h
 * ADC1 continuo, calibración del offset de los sensores, protección por
 * sobrecorriente y valor eficaz.
 *
 * El ADC muestrea tres sensores (CH1, U, V) por DMA. Una tarea en el núcleo 0
 * procesa las tramas, compara el pico con el umbral de cada canal y calcula
 * el valor eficaz. La corriente de la fase W se obtiene como -(U + V).
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "estado.h"

/* Índices de los sensores físicos; SENSOR_NUM es la cantidad (3). */
enum { SENSOR_CH1 = 0, SENSOR_U, SENSOR_V, SENSOR_NUM };

/* Se llama desde la tarea de protección cuando una corriente supera el
 * umbral: causa de la falla, pico medido en A y frecuencia f1 en ese momento. */
typedef void (*medicion_cb_disparo_t)(causa_falla_t causa, float pico_a, float f1_hz);
/* Se llama al terminar la calibración: ok = true si los tres offsets están
 * dentro de OFFSET_MIN_MV y OFFSET_MAX_MV. */
typedef void (*medicion_cb_calib_t)(bool ok);

typedef struct {
    float rms_a[4];             /* CH1, U, V, W */
    int offset_mv[SENSOR_NUM];  /* offset medido de cada sensor, en mV */
    bool offset_ok[SENSOR_NUM]; /* true si ese offset está en rango */
    bool calibrado;             /* true tras una calibración exitosa */
    uint32_t desbordes;         /* tramas perdidas del DMA */
} medicion_datos_t;

/* Configura el ADC1 continuo y lanza la tarea de protección. */
esp_err_t medicion_iniciar(medicion_cb_disparo_t cb_disparo, medicion_cb_calib_t cb_calib);
/* Mide el offset con los drivers apagados. El resultado llega por cb_calib. */
void medicion_calibrar(void);
/* Copia una instantánea de las mediciones para la interfaz. */
void medicion_leer(medicion_datos_t *d);
