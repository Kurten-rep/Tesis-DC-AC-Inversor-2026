/*
 * potencia.h
 * MCPWM de los dos canales, líneas EN, paro de emergencia y tarea de rampa.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "estado.h"

/* Gancho que se ejecuta en la ISR de falla cuando el paro pasa a activo.
 * Debe estar en IRAM y devolver true si despertó una tarea de mayor
 * prioridad. */
typedef bool (*potencia_cb_paro_t)(void);

typedef struct {
    canal_t canal;          /* canal que está modulando, o CANAL_NINGUNO */
    float f1_hz;
    float f_ref_hz;
    float fsw_hz;
    float ma;
    bool sincrono;          /* Canal 2 con fsw = 21·f1 */
    bool rampa_completa;
} potencia_estado_t;

/* Lleva EN_CH1 y EN_CH2 al nivel inactivo. Es lo primero que corre. */
void potencia_pines_seguros(void);

/* Configura los dos MCPWM detenidos y frenados, y lanza la tarea de rampa.
 * Se debe llamar desde una tarea fijada al núcleo 1, para que la ISR del
 * MCPWM quede en ese núcleo. */
esp_err_t potencia_iniciar(potencia_cb_paro_t cb_paro);

bool potencia_paro_activo(void);
bool potencia_canal_deshabilitado(canal_t c);
canal_t potencia_canal_activo(void);
float potencia_f1_actual(void);

/* Apagado completo: EN inactivo, salidas frenadas en bajo, temporizadores
 * detenidos. */
void potencia_apagar_todo(void);

/* Disparo por sobrecorriente desde la tarea de protección: freno por
 * software en los dos grupos y EN inactivo. No espera nada. */
void potencia_disparo(void);

bool potencia_arrancar(canal_t c, uint32_t f_ref_hz, uint32_t fsw_base_hz);
void potencia_habilitar(canal_t c);
void potencia_fijar_fref(uint32_t f_ref_hz);
bool potencia_recuperar_falla(void);
void potencia_leer_estado(potencia_estado_t *e);
