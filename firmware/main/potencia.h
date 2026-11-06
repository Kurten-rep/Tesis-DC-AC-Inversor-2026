/*
 * potencia.h
 * MCPWM de los dos canales, líneas EN, paro de emergencia y tarea de rampa.
 *
 * Este módulo es el único que mueve los pines de potencia. El resto del
 * firmware (máquina de estados, interfaz) le pide acciones por estas
 * funciones y nunca toca los GPIO de HIN, LIN o EN directamente.
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

/* Instantánea del estado de la modulación, para mostrar en pantalla. */
typedef struct {
    canal_t canal;          /* canal que está modulando, o CANAL_NINGUNO */
    float f1_hz;            /* frecuencia de salida actual (sigue a la rampa) */
    float f_ref_hz;         /* frecuencia pedida por el usuario */
    float fsw_hz;           /* frecuencia de conmutación actual */
    float ma;               /* índice de modulación actual, 0.0 a 1.0 */
    bool sincrono;          /* Canal 2 con fsw = 21·f1 */
    bool rampa_completa;    /* Canal 1: ma llegó a su valor final. Canal 2: f1 alcanzó f_ref */
} potencia_estado_t;

/* Lleva EN_CH1 y EN_CH2 al nivel inactivo. Es lo primero que corre. */
void potencia_pines_seguros(void);

/* Configura los dos MCPWM detenidos y frenados, y lanza la tarea de rampa.
 * Se debe llamar desde una tarea fijada al núcleo 1, para que la ISR del
 * MCPWM quede en ese núcleo. */
esp_err_t potencia_iniciar(potencia_cb_paro_t cb_paro);

/* true si el pin de paro está en nivel de paro (pulsado o cable cortado). */
bool potencia_paro_activo(void);
/* true si el EN del canal c está en nivel inactivo (drivers apagados). */
bool potencia_canal_deshabilitado(canal_t c);
/* Canal que está modulando ahora, o CANAL_NINGUNO. */
canal_t potencia_canal_activo(void);
/* Frecuencia de salida actual en Hz. */
float potencia_f1_actual(void);

/* Apagado completo: EN inactivo, salidas frenadas en bajo, temporizadores
 * detenidos. */
void potencia_apagar_todo(void);

/* Disparo por sobrecorriente desde la tarea de protección: freno por
 * software en los dos grupos y EN inactivo. No espera nada. */
void potencia_disparo(void);

/* Libera el freno y arranca el MCPWM del canal con ma = 0, sin habilitar
 * todavía los drivers. Devuelve false si algo falla. */
bool potencia_arrancar(canal_t c, uint32_t f_ref_hz, uint32_t fsw_base_hz);
/* Pone EN en nivel activo para el canal, que desde ahí puede conmutar. */
void potencia_habilitar(canal_t c);
/* Cambia la frecuencia de referencia con el canal ya operando (Canal 2). */
void potencia_fijar_fref(uint32_t f_ref_hz);
/* Libera el freno por falla tras reconocer el error. false si no se pudo. */
bool potencia_recuperar_falla(void);
/* Copia el estado actual de la modulación. */
void potencia_leer_estado(potencia_estado_t *e);
