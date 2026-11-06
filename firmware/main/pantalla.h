/*
 * pantalla.h
 * Bus SPI2, ILI9341, táctil XPT2046 y LVGL. Todas las llamadas a LVGL se
 * hacen desde la tarea de LVGL; el resto del firmware pasa por ui.c.
 *
 * LVGL no es seguro entre hilos: si dos tareas lo llaman a la vez se
 * corrompe la memoria. Por eso solo una tarea lo toca.
 */
#pragma once
#include "esp_err.h"

/* Función que se llama una vez, dentro de la tarea de LVGL, cuando la
 * pantalla ya está lista para construir la interfaz. */
typedef void (*pantalla_cb_listo_t)(void);

/* Arranca la pantalla y la tarea de LVGL en el núcleo 0. cb_listo corre
 * dentro de la tarea de LVGL, una vez, para construir la interfaz. */
esp_err_t pantalla_iniciar(pantalla_cb_listo_t cb_listo);
