/*
 * pantalla.h
 * Bus SPI2, ILI9341, táctil XPT2046 y LVGL. Todas las llamadas a LVGL se
 * hacen desde la tarea de LVGL; el resto del firmware pasa por ui.c.
 */
#pragma once
#include "esp_err.h"

typedef void (*pantalla_cb_listo_t)(void);

/* Arranca la pantalla y la tarea de LVGL en el núcleo 0. cb_listo corre
 * dentro de la tarea de LVGL, una vez, para construir la interfaz. */
esp_err_t pantalla_iniciar(pantalla_cb_listo_t cb_listo);
