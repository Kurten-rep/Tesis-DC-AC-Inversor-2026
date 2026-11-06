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

/* Canal del inversor. CANAL_1 es el monofásico y CANAL_2 el trifásico.
 * Los valores 1 y 2 se usan tal cual en los eventos de la interfaz. */
typedef enum {
    CANAL_NINGUNO = 0,
    CANAL_1 = 1,
    CANAL_2 = 2,
} canal_t;

/* Estados del equipo, en el orden en que se recorren normalmente. */
typedef enum {
    EST_ARRANQUE = 0,       /* calibrando sensores tras el encendido */
    EST_ERROR_SENSOR,       /* calibración fallida (solo con SENSORES_BLOQUEAN_INTERFAZ = 1) */
    EST_ESPERA,             /* menú de selección de canal */
    EST_CONFIGURACION,      /* ajuste de parámetros del canal elegido */
    EST_ARMADO,             /* espera de 500 ms con compuertas apagadas */
    EST_OPERACION,          /* el canal está modulando */
    EST_FALLA,              /* sobrecorriente, paro o error interno */
    EST_NUM                 /* cantidad de estados, no es un estado */
} estado_t;

/* Motivo por el que el equipo entró en EST_FALLA. */
typedef enum {
    CAUSA_NINGUNA = 0,
    CAUSA_SOBRECORRIENTE_CH1,
    CAUSA_SOBRECORRIENTE_U,
    CAUSA_SOBRECORRIENTE_V,
    CAUSA_SOBRECORRIENTE_W,
    CAUSA_PARO,
    CAUSA_ERROR_INTERNO,
} causa_falla_t;

/* Eventos que reciben las transiciones. Vienen de los botones, de la ISR
 * del paro y de la tarea de protección. */
typedef enum {
    EV_CALIB_OK = 0,        /* los tres offsets están en rango */
    EV_CALIB_ERROR,         /* algún offset fuera de rango */
    EV_REINTENTAR,          /* botón de reintentar la calibración */
    EV_SELECCIONAR_CANAL,   /* valor = canal_t */
    EV_VOLVER,
    EV_INICIAR,
    EV_DETENER,
    EV_AJUSTAR_FREF,        /* valor = cambio en Hz, con signo */
    EV_SELECCIONAR_FSW,     /* valor = fsw base en Hz */
    EV_SOBRECORRIENTE,      /* causa, pico_a, valor = f1 en Hz al disparo */
    EV_PARO,                /* valor = f1 en Hz */
    EV_RECONOCER,           /* botón de aceptar la falla y volver al inicio */
} tipo_evento_t;

/* Un evento con sus datos. Cada tipo usa solo los campos que indica el
 * comentario del enum de arriba; los demás quedan en cero. */
typedef struct {
    tipo_evento_t tipo;
    int32_t valor;
    causa_falla_t causa;
    float pico_a;
} evento_t;

/* Parámetros que el usuario elige en la pantalla de configuración. */
typedef struct {
    canal_t canal;
    uint32_t f_ref_hz;      /* Canal 2 */
    uint32_t fsw_base_hz;   /* Canal 2: 10 kHz o 20 kHz */
} parametros_t;

/* Funciones de hardware que la máquina de estados necesita. En el equipo
 * las implementa main.c con potencia.c; en las pruebas del PC son simuladas.
 * Todas reciben "ctx" para que quien las implementa guarde su contexto. */
typedef struct {
    bool (*paro_activo)(void *ctx);                  /* paro presionado */
    bool (*canal_deshabilitado)(void *ctx, canal_t c); /* EN del canal inactivo */
    void (*apagar_todo)(void *ctx);                  /* EN inactivo y MCPWM detenido */
    /* Recupera el freno de falla y arranca el MCPWM del canal con ma = 0.
     * Los drivers siguen deshabilitados. */
    bool (*arrancar)(void *ctx, canal_t c, const parametros_t *p);
    void (*habilitar)(void *ctx, canal_t c);         /* activa EN del canal */
    void (*actualizar_fref)(void *ctx, uint32_t f_ref_hz); /* cambia f1 en marcha */
    bool (*recuperar_falla)(void *ctx);              /* libera el freno de falla */
    void *ctx;
} sm_hw_t;

/* Estado completo de la máquina. Lo inicializa sm_iniciar(). */
typedef struct {
    estado_t estado;
    parametros_t par;
    uint32_t t_entrada_ms;  /* instante en que se entró al estado actual */
    causa_falla_t causa;    /* motivo de la última falla */
    canal_t canal_falla;    /* canal que estaba activo al fallar */
    float pico_a;           /* pico de corriente medido al disparo */
    int32_t f1_falla_hz;    /* frecuencia de salida al fallar */
    bool sensores_ok;       /* false: offsets fuera de rango, drivers bloqueados */
    const char *aviso;      /* mensaje corto para la pantalla, o NULL */
    uint32_t t_aviso_ms;    /* instante en que se publicó el aviso */
    const sm_hw_t *hw;
} sm_t;

/* Deja la máquina en EST_ARRANQUE con los valores por defecto. */
void sm_iniciar(sm_t *sm, const sm_hw_t *hw, uint32_t ahora_ms);
/* Procesa un evento y hace la transición que corresponda. */
void sm_evento(sm_t *sm, const evento_t *ev, uint32_t ahora_ms);
/* Se llama periódicamente: apaga avisos viejos y avanza el estado ARMADO. */
void sm_tick(sm_t *sm, uint32_t ahora_ms);
/* Texto del estado para la barra superior y el registro. */
const char *sm_nombre_estado(estado_t e);
/* Texto de la causa de la falla para la pantalla de falla. */
const char *sm_texto_causa(causa_falla_t c);
/* Limita la frecuencia de referencia a [CH2_F1_MIN_HZ, CH2_F1_MAX_HZ]. */
uint32_t sm_limitar_fref(int32_t f_hz);
