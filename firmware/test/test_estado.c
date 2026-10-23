/*
 * test_estado.c
 * Prueba en PC de la máquina de estados con hardware simulado.
 *   make -C firmware/test
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "estado.h"
#include "config.h"

static struct {
    bool paro;
    bool en[3];           /* en[1], en[2] */
    int n_apagar, n_arrancar, n_habilitar;
    canal_t ult_arrancar;
    parametros_t ult_par;
    uint32_t fref;
    bool forzar_en_inactivo;  /* simula un EN pegado */
} hw;

static bool m_paro(void *c) { return hw.paro; }
static bool m_deshab(void *c, canal_t ch) { return !hw.en[ch] && !hw.forzar_en_inactivo; }
static void m_apagar(void *c) { hw.en[1] = hw.en[2] = false; hw.n_apagar++; }
static bool m_arrancar(void *c, canal_t ch, const parametros_t *p) { hw.n_arrancar++; hw.ult_arrancar = ch; hw.ult_par = *p; return true; }
static void m_habilitar(void *c, canal_t ch)
{
    hw.en[ch] = true;
    hw.n_habilitar++;
    assert(!(hw.en[1] && hw.en[2]) && "los dos canales habilitados a la vez");
}
static void m_fref(void *c, uint32_t f) { hw.fref = f; }
static bool m_recuperar(void *c) { return !hw.paro; }

static const sm_hw_t HW = { m_paro, m_deshab, m_apagar, m_arrancar, m_habilitar, m_fref, m_recuperar, NULL };

static void ev(sm_t *sm, tipo_evento_t t, int32_t v, uint32_t ms)
{
    evento_t e = { .tipo = t, .valor = v };
    sm_evento(sm, &e, ms);
}

static int n_ok;
#define OK(cond) do { assert(cond); n_ok++; } while (0)

int main(void)
{
    sm_t sm;
    memset(&hw, 0, sizeof hw);
    sm_iniciar(&sm, &HW, 0);
    OK(sm.estado == EST_ARRANQUE);
    OK(hw.n_apagar == 1);

    /* Error de sensor y reintento */
    ev(&sm, EV_CALIB_ERROR, 0, 10);
    OK(sm.estado == EST_ERROR_SENSOR);
    ev(&sm, EV_SELECCIONAR_CANAL, CANAL_1, 20);
    OK(sm.estado == EST_ERROR_SENSOR);       /* bloqueado */
    ev(&sm, EV_REINTENTAR, 0, 30);
    OK(sm.estado == EST_ARRANQUE);
    ev(&sm, EV_CALIB_OK, 0, 40);
    OK(sm.estado == EST_ESPERA);

    /* Paro en ESPERA: no es falla */
    ev(&sm, EV_PARO, 0, 50);
    OK(sm.estado == EST_ESPERA);

    /* Configuración del Canal 2 */
    ev(&sm, EV_SELECCIONAR_CANAL, CANAL_2, 60);
    OK(sm.estado == EST_CONFIGURACION && sm.par.canal == CANAL_2);
    OK(sm.par.f_ref_hz == CH2_F1_DEFECTO_HZ);
    ev(&sm, EV_AJUSTAR_FREF, 100, 61);
    OK(sm.par.f_ref_hz == CH2_F1_DEFECTO_HZ + 100);
    ev(&sm, EV_AJUSTAR_FREF, -5000, 62);
    OK(sm.par.f_ref_hz == CH2_F1_MIN_HZ);
    ev(&sm, EV_AJUSTAR_FREF, 5000, 63);
    OK(sm.par.f_ref_hz == CH2_F1_MAX_HZ);
    ev(&sm, EV_AJUSTAR_FREF, -600, 64);
    OK(sm.par.f_ref_hz == 600);
    ev(&sm, EV_SELECCIONAR_FSW, 20000, 65);
    OK(sm.par.fsw_base_hz == 20000);
    ev(&sm, EV_SELECCIONAR_FSW, 15000, 66);   /* no válido */
    OK(sm.par.fsw_base_hz == 20000);

    /* INICIAR con el paro presionado */
    hw.paro = true;
    ev(&sm, EV_INICIAR, 0, 100);
    OK(sm.estado == EST_CONFIGURACION && sm.aviso != NULL);
    hw.paro = false;

    /* ARMADO: 500 ms sin arrancar */
    ev(&sm, EV_INICIAR, 0, 1000);
    OK(sm.estado == EST_ARMADO);
    sm_tick(&sm, 1100);
    sm_tick(&sm, 1499);
    OK(sm.estado == EST_ARMADO && hw.n_arrancar == 0 && !hw.en[2]);
    sm_tick(&sm, 1500);
    OK(sm.estado == EST_OPERACION);
    OK(hw.n_arrancar == 1 && hw.ult_arrancar == CANAL_2);
    OK(hw.ult_par.f_ref_hz == 600 && hw.ult_par.fsw_base_hz == 20000);
    OK(hw.en[2] && !hw.en[1]);

    /* Ajuste de frecuencia en operación */
    ev(&sm, EV_AJUSTAR_FREF, 10, 1600);
    OK(sm.par.f_ref_hz == 610 && hw.fref == 610);

    /* Sobrecorriente */
    evento_t oc = { .tipo = EV_SOBRECORRIENTE, .causa = CAUSA_SOBRECORRIENTE_U, .pico_a = 15.8f, .valor = 452 };
    int apagados = hw.n_apagar;
    sm_evento(&sm, &oc, 2000);
    OK(sm.estado == EST_FALLA && sm.causa == CAUSA_SOBRECORRIENTE_U);
    OK(sm.canal_falla == CANAL_2 && sm.f1_falla_hz == 452);
    OK(hw.n_apagar == apagados + 1 && !hw.en[2]);

    /* Reconocer con el paro presionado: sigue en falla */
    hw.paro = true;
    ev(&sm, EV_RECONOCER, 0, 2100);
    OK(sm.estado == EST_FALLA);
    hw.paro = false;
    ev(&sm, EV_RECONOCER, 0, 2200);
    OK(sm.estado == EST_ESPERA && sm.par.canal == CANAL_NINGUNO);

    /* Canal 1, paro durante la operación */
    ev(&sm, EV_SELECCIONAR_CANAL, CANAL_1, 3000);
    ev(&sm, EV_INICIAR, 0, 3010);
    sm_tick(&sm, 3510);
    OK(sm.estado == EST_OPERACION && hw.en[1] && !hw.en[2]);
    ev(&sm, EV_PARO, 0, 3600);
    OK(sm.estado == EST_FALLA && sm.causa == CAUSA_PARO && !hw.en[1]);
    ev(&sm, EV_RECONOCER, 0, 3700);
    OK(sm.estado == EST_ESPERA);

    /* Paro presionado durante el ARMADO */
    ev(&sm, EV_SELECCIONAR_CANAL, CANAL_1, 4000);
    ev(&sm, EV_INICIAR, 0, 4010);
    hw.paro = true;
    sm_tick(&sm, 4100);
    OK(sm.estado == EST_FALLA && sm.causa == CAUSA_PARO && hw.n_habilitar == 2);
    hw.paro = false;
    ev(&sm, EV_RECONOCER, 0, 4200);

    /* EN del canal inactivo pegado: error interno, nunca se habilita */
    ev(&sm, EV_SELECCIONAR_CANAL, CANAL_2, 5000);
    ev(&sm, EV_INICIAR, 0, 5010);
    hw.forzar_en_inactivo = true;
    sm_tick(&sm, 5600);
    OK(sm.estado == EST_FALLA && sm.causa == CAUSA_ERROR_INTERNO);
    OK(hw.n_habilitar == 2);
    hw.forzar_en_inactivo = false;
    ev(&sm, EV_RECONOCER, 0, 5700);

    /* DETENER */
    ev(&sm, EV_SELECCIONAR_CANAL, CANAL_1, 6000);
    ev(&sm, EV_INICIAR, 0, 6010);
    sm_tick(&sm, 6510);
    OK(sm.estado == EST_OPERACION);
    ev(&sm, EV_DETENER, 0, 7000);
    OK(sm.estado == EST_ESPERA && !hw.en[1] && !hw.en[2]);

    /* Sobrecorriente fuera de operación se ignora */
    sm_evento(&sm, &oc, 7100);
    OK(sm.estado == EST_ESPERA);

    printf("test_estado: %d comprobaciones correctas\n", n_ok);
    return 0;
}
