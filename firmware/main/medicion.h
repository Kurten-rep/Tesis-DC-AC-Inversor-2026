/*
 * medicion.h
 * ADC1 continuo, calibración del offset de los sensores, protección por
 * sobrecorriente y valor eficaz.
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "estado.h"

enum { SENSOR_CH1 = 0, SENSOR_U, SENSOR_V, SENSOR_NUM };

typedef void (*medicion_cb_disparo_t)(causa_falla_t causa, float pico_a, float f1_hz);
typedef void (*medicion_cb_calib_t)(bool ok);

typedef struct {
    float rms_a[4];             /* CH1, U, V, W */
    int offset_mv[SENSOR_NUM];
    bool offset_ok[SENSOR_NUM];
    bool calibrado;
    uint32_t desbordes;         /* tramas perdidas del DMA */
} medicion_datos_t;

esp_err_t medicion_iniciar(medicion_cb_disparo_t cb_disparo, medicion_cb_calib_t cb_calib);
/* Mide el offset con los drivers apagados. El resultado llega por cb_calib. */
void medicion_calibrar(void);
void medicion_leer(medicion_datos_t *d);
