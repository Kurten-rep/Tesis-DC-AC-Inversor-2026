/*
 * estado.h
 * Máquina de estados del equipo (Figura 27 del documento).
 *
 * El módulo no toca el hardware: recibe un conjunto de funciones (sm_hw_t)
 * para leer el paro, apagar, arrancar y habilitar los canales. Así se
 * prueba en el PC con funciones simuladas (test/test_estado.c).
 *
 * La protección no depende de esta máquina: la ISR de falla y la tarea de
 * protección apagan el hardware por su cuenta y después avisan con un
 * evento. Aquí solo se registra la falla y se decide qué mostrar.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CANAL_NINGUNO = 0,
    CANAL_1 = 1,
    CANAL_2 = 2,
} canal_t;

typedef enum {
    EST_ARRANQUE = 0,
    EST_ERROR_SENSOR,
    EST_ESPERA,
    EST_CONFIGURACION,
    EST_ARMADO,
    EST_OPERACION,
    EST_FALLA,
    EST_NUM
} estado_t;

typedef enum {
    CAUSA_NINGUNA = 0,
    CAUSA_SOBRECORRIENTE_CH1,
    CAUSA_SOBRECORRIENTE_U,
    CAUSA_SOBRECORRIENTE_V,
    CAUSA_SOBRECORRIENTE_W,
    CAUSA_PARO,
    CAUSA_ERROR_INTERNO,
} causa_falla_t;

typedef enum {
    EV_CALIB_OK = 0,
    EV_CALIB_ERROR,
    EV_REINTENTAR,
    EV_SELECCIONAR_CANAL,   /* valor = canal_t */
    EV_VOLVER,
    EV_INICIAR,
    EV_DETENER,
    EV_AJUSTAR_FREF,        /* valor = cambio en Hz, con signo */
    EV_SELECCIONAR_FSW,     /* valor = fsw base en Hz */
    EV_SOBRECORRIENTE,      /* causa, pico_a, valor = f1 en Hz al disparo */
    EV_PARO,                /* valor = f1 en Hz */
    EV_RECONOCER,
} tipo_evento_t;

typedef struct {
    tipo_evento_t tipo;
    int32_t valor;
    causa_falla_t causa;
    float pico_a;
} evento_t;

typedef struct {
    canal_t canal;
    uint32_t f_ref_hz;      /* Canal 2 */
    uint32_t fsw_base_hz;   /* Canal 2: 10 kHz o 20 kHz */
} parametros_t;

typedef struct {
    bool (*paro_activo)(void *ctx);
    bool (*canal_deshabilitado)(void *ctx, canal_t c);
    void (*apagar_todo)(void *ctx);
    /* Recupera el freno de falla y arranca el MCPWM del canal con ma = 0.
     * Los drivers siguen deshabilitados. */
    bool (*arrancar)(void *ctx, canal_t c, const parametros_t *p);
    void (*habilitar)(void *ctx, canal_t c);
    void (*actualizar_fref)(void *ctx, uint32_t f_ref_hz);
    bool (*recuperar_falla)(void *ctx);
    void *ctx;
} sm_hw_t;

typedef struct {
    estado_t estado;
    parametros_t par;
    uint32_t t_entrada_ms;
    causa_falla_t causa;
    canal_t canal_falla;
    float pico_a;
    int32_t f1_falla_hz;
    const char *aviso;      /* mensaje corto para la pantalla, o NULL */
    uint32_t t_aviso_ms;
    const sm_hw_t *hw;
} sm_t;

void sm_iniciar(sm_t *sm, const sm_hw_t *hw, uint32_t ahora_ms);
void sm_evento(sm_t *sm, const evento_t *ev, uint32_t ahora_ms);
void sm_tick(sm_t *sm, uint32_t ahora_ms);
const char *sm_nombre_estado(estado_t e);
const char *sm_texto_causa(causa_falla_t c);
uint32_t sm_limitar_fref(int32_t f_hz);
