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

/* Función que la interfaz llama cuando el usuario toca un botón; recibe el
 * evento que la máquina de estados debe procesar. */
typedef void (*ui_cb_evento_t)(const evento_t *ev);

/* Todo lo que la interfaz necesita para dibujarse. La tarea de interfaz lo
 * arma cada 50 ms y lo pasa a ui_actualizar(). */
typedef struct {
    const sm_t *sm;             /* máquina de estados (solo lectura) */
    uint32_t ahora_ms;          /* reloj del sistema en ms */
    bool paro;                  /* estado del paro de emergencia */
    potencia_estado_t pot;      /* estado de la modulación */
    medicion_datos_t med;       /* corrientes y estado de los sensores */
} ui_datos_t;

/* Construye todas las pantallas. cb recibe los eventos de los botones. */
void ui_iniciar(ui_cb_evento_t cb);
/* Cambia de pantalla según el estado y refresca los valores mostrados. */
void ui_actualizar(const ui_datos_t *d);
