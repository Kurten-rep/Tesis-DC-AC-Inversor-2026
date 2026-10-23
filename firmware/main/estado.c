/*
 * estado.c
 * Transiciones:
 *
 *   ARRANQUE --calib ok--> ESPERA        ARRANQUE --calib error--> ERROR_SENSOR
 *   ERROR_SENSOR --reintentar--> ARRANQUE
 *   ESPERA --canal--> CONFIGURACION --volver--> ESPERA
 *   CONFIGURACION --iniciar (paro liberado)--> ARMADO
 *   ARMADO: apaga todo, verifica el canal inactivo y el paro, espera 500 ms,
 *           arranca el MCPWM con ma = 0, habilita el canal --> OPERACION
 *   ARMADO/OPERACION --detener--> ESPERA
 *   ARMADO/OPERACION --sobrecorriente o paro--> FALLA
 *   FALLA --reconocer (paro liberado)--> ESPERA
 */
#include "estado.h"
#include "config.h"
#include <stddef.h>

#define AVISO_MS 2500u

static void entrar(sm_t *sm, estado_t e, uint32_t ahora_ms)
{
    sm->estado = e;
    sm->t_entrada_ms = ahora_ms;
}

static void avisar(sm_t *sm, const char *txt, uint32_t ahora_ms)
{
    sm->aviso = txt;
    sm->t_aviso_ms = ahora_ms;
}

static void ir_a_falla(sm_t *sm, causa_falla_t causa, float pico, int32_t f1, uint32_t ahora_ms)
{
    sm->hw->apagar_todo(sm->hw->ctx);
    sm->causa = causa;
    sm->canal_falla = sm->par.canal;
    sm->pico_a = pico;
    sm->f1_falla_hz = f1;
    entrar(sm, EST_FALLA, ahora_ms);
}

uint32_t sm_limitar_fref(int32_t f_hz)
{
    if (f_hz < (int32_t)CH2_F1_MIN_HZ) {
        return CH2_F1_MIN_HZ;
    }
    if (f_hz > (int32_t)CH2_F1_MAX_HZ) {
        return CH2_F1_MAX_HZ;
    }
    return (uint32_t)f_hz;
}

void sm_iniciar(sm_t *sm, const sm_hw_t *hw, uint32_t ahora_ms)
{
    sm->hw = hw;
    sm->par.canal = CANAL_NINGUNO;
    sm->par.f_ref_hz = CH2_F1_DEFECTO_HZ;
    sm->par.fsw_base_hz = CH2_FSW_BASE_DEFECTO;
    sm->causa = CAUSA_NINGUNA;
    sm->canal_falla = CANAL_NINGUNO;
    sm->pico_a = 0.0f;
    sm->f1_falla_hz = 0;
    sm->aviso = NULL;
    sm->t_aviso_ms = 0;
    hw->apagar_todo(hw->ctx);
    entrar(sm, EST_ARRANQUE, ahora_ms);
}

void sm_evento(sm_t *sm, const evento_t *ev, uint32_t ahora_ms)
{
    const sm_hw_t *hw = sm->hw;

    /* Fallas: válidas mientras haya un canal armado u operando */
    if (ev->tipo == EV_SOBRECORRIENTE || ev->tipo == EV_PARO) {
        if (sm->estado == EST_ARMADO || sm->estado == EST_OPERACION) {
            causa_falla_t c = (ev->tipo == EV_PARO) ? CAUSA_PARO : ev->causa;
            ir_a_falla(sm, c, ev->pico_a, ev->valor, ahora_ms);
        }
        /* En los demás estados los drivers ya están apagados; el paro se
         * muestra en la barra de estado y bloquea INICIAR. */
        return;
    }

    switch (sm->estado) {
    case EST_ARRANQUE:
        if (ev->tipo == EV_CALIB_OK) {
            entrar(sm, EST_ESPERA, ahora_ms);
        } else if (ev->tipo == EV_CALIB_ERROR) {
            entrar(sm, EST_ERROR_SENSOR, ahora_ms);
        }
        break;

    case EST_ERROR_SENSOR:
        if (ev->tipo == EV_REINTENTAR) {
            entrar(sm, EST_ARRANQUE, ahora_ms);
        }
        break;

    case EST_ESPERA:
        if (ev->tipo == EV_SELECCIONAR_CANAL &&
            (ev->valor == CANAL_1 || ev->valor == CANAL_2)) {
            sm->par.canal = (canal_t)ev->valor;
            sm->aviso = NULL;
            entrar(sm, EST_CONFIGURACION, ahora_ms);
        }
        break;

    case EST_CONFIGURACION:
        switch (ev->tipo) {
        case EV_VOLVER:
            sm->par.canal = CANAL_NINGUNO;
            entrar(sm, EST_ESPERA, ahora_ms);
            break;
        case EV_AJUSTAR_FREF:
            if (sm->par.canal == CANAL_2) {
                sm->par.f_ref_hz = sm_limitar_fref((int32_t)sm->par.f_ref_hz + ev->valor);
            }
            break;
        case EV_SELECCIONAR_FSW:
            if (sm->par.canal == CANAL_2 && (ev->valor == 10000 || ev->valor == 20000)) {
                sm->par.fsw_base_hz = (uint32_t)ev->valor;
            }
            break;
        case EV_INICIAR:
            if (hw->paro_activo(hw->ctx)) {
                avisar(sm, "Libere el paro de emergencia", ahora_ms);
            } else {
                hw->apagar_todo(hw->ctx);
                entrar(sm, EST_ARMADO, ahora_ms);
            }
            break;
        default:
            break;
        }
        break;

    case EST_ARMADO:
        if (ev->tipo == EV_DETENER) {
            hw->apagar_todo(hw->ctx);
            sm->par.canal = CANAL_NINGUNO;
            entrar(sm, EST_ESPERA, ahora_ms);
        }
        break;

    case EST_OPERACION:
        if (ev->tipo == EV_DETENER) {
            hw->apagar_todo(hw->ctx);
            sm->par.canal = CANAL_NINGUNO;
            entrar(sm, EST_ESPERA, ahora_ms);
        } else if (ev->tipo == EV_AJUSTAR_FREF && sm->par.canal == CANAL_2) {
            sm->par.f_ref_hz = sm_limitar_fref((int32_t)sm->par.f_ref_hz + ev->valor);
            hw->actualizar_fref(hw->ctx, sm->par.f_ref_hz);
        }
        break;

    case EST_FALLA:
        if (ev->tipo == EV_RECONOCER) {
            if (hw->paro_activo(hw->ctx)) {
                avisar(sm, "El paro sigue presionado", ahora_ms);
            } else if (!hw->recuperar_falla(hw->ctx)) {
                avisar(sm, "No se pudo liberar la falla", ahora_ms);
            } else {
                sm->causa = CAUSA_NINGUNA;
                sm->par.canal = CANAL_NINGUNO;
                entrar(sm, EST_ESPERA, ahora_ms);
            }
        }
        break;

    default:
        break;
    }
}

void sm_tick(sm_t *sm, uint32_t ahora_ms)
{
    const sm_hw_t *hw = sm->hw;

    if (sm->aviso != NULL && (ahora_ms - sm->t_aviso_ms) > AVISO_MS) {
        sm->aviso = NULL;
    }

    if (sm->estado != EST_ARMADO) {
        return;
    }

    /* Durante toda la espera se exige que el canal inactivo siga con SD en
     * alto y que el paro esté liberado. */
    canal_t inactivo = (sm->par.canal == CANAL_1) ? CANAL_2 : CANAL_1;
    if (!hw->canal_deshabilitado(hw->ctx, inactivo)) {
        ir_a_falla(sm, CAUSA_ERROR_INTERNO, 0.0f, 0, ahora_ms);
        return;
    }
    if (hw->paro_activo(hw->ctx)) {
        ir_a_falla(sm, CAUSA_PARO, 0.0f, 0, ahora_ms);
        return;
    }
    if ((ahora_ms - sm->t_entrada_ms) < ARMADO_ESPERA_MS) {
        return;
    }
    if (!hw->arrancar(hw->ctx, sm->par.canal, &sm->par)) {
        ir_a_falla(sm, CAUSA_ERROR_INTERNO, 0.0f, 0, ahora_ms);
        return;
    }
    hw->habilitar(hw->ctx, sm->par.canal);
    entrar(sm, EST_OPERACION, ahora_ms);
}

const char *sm_nombre_estado(estado_t e)
{
    static const char *const nombres[EST_NUM] = {
        "ARRANQUE", "ERROR SENSOR", "ESPERA", "CONFIGURACIÓN",
        "ARMADO", "OPERACIÓN", "FALLA",
    };
    return (e < EST_NUM) ? nombres[e] : "?";
}

const char *sm_texto_causa(causa_falla_t c)
{
    switch (c) {
    case CAUSA_SOBRECORRIENTE_CH1: return "Sobrecorriente en el Canal 1";
    case CAUSA_SOBRECORRIENTE_U:   return "Sobrecorriente en fase U";
    case CAUSA_SOBRECORRIENTE_V:   return "Sobrecorriente en fase V";
    case CAUSA_SOBRECORRIENTE_W:   return "Sobrecorriente en fase W";
    case CAUSA_PARO:               return "Paro de emergencia";
    case CAUSA_ERROR_INTERNO:      return "Error interno de habilitación";
    default:                       return "Sin falla";
    }
}
