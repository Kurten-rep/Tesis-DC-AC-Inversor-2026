/*
 * ui.h
 * Pantallas del equipo (lienzo "Interfaz TFT Inversor"). Solo se llama
 * desde la tarea de LVGL.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "estado.h"
#include "potencia.h"
#include "medicion.h"

typedef void (*ui_cb_evento_t)(const evento_t *ev);

typedef struct {
    const sm_t *sm;
    uint32_t ahora_ms;
    bool paro;
    potencia_estado_t pot;
    medicion_datos_t med;
} ui_datos_t;

void ui_iniciar(ui_cb_evento_t cb);
void ui_actualizar(const ui_datos_t *d);
