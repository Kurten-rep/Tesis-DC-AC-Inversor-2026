/*
 * main.c
 * Arranque del firmware (Figura 28a) y tarea de interfaz (Figura 28b).
 *
 * Reparto de núcleos (sección 4.3.8):
 *   Núcleo 1: ISR del MCPWM (SPWM) y tarea de rampa, cada 1 ms.
 *   Núcleo 0: tarea de protección y medición (ADC1 por DMA) y tarea de LVGL,
 *             que cada 50 ms lee los eventos, avanza la máquina de estados y
 *             refresca la pantalla.
 *
 * Los eventos llegan por una cola: de los botones, de la ISR del paro y de la
 * tarea de protección. Solo la tarea de interfaz toca la máquina de estados.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "config.h"
#include "estado.h"
#include "potencia.h"
#include "medicion.h"
#include "pantalla.h"
#include "ui.h"
#include "banco.h"

static const char *TAG = "main";

static QueueHandle_t s_cola;
static sm_t s_sm;
static bool s_calib_pedida;

static uint32_t ahora_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------ Fuentes de eventos */

static IRAM_ATTR bool cb_paro_isr(void)
{
    BaseType_t despierta = pdFALSE;
    evento_t ev = { .tipo = EV_PARO, .causa = CAUSA_PARO };
    xQueueSendFromISR(s_cola, &ev, &despierta);
    return despierta == pdTRUE;
}

static void cb_disparo(causa_falla_t causa, float pico_a, float f1_hz)
{
    evento_t ev = { .tipo = EV_SOBRECORRIENTE, .causa = causa, .pico_a = pico_a, .valor = (int32_t)f1_hz };
    xQueueSend(s_cola, &ev, 0);
}

static void cb_calib(bool ok)
{
    evento_t ev = { .tipo = ok ? EV_CALIB_OK : EV_CALIB_ERROR };
    xQueueSend(s_cola, &ev, 0);
}

static void cb_ui(const evento_t *ev)
{
    xQueueSend(s_cola, ev, 0);
}

/* ----------------------------------- Funciones de hardware para la SM */

static bool hw_paro(void *ctx) { return potencia_paro_activo(); }
static bool hw_deshab(void *ctx, canal_t c) { return potencia_canal_deshabilitado(c); }
static void hw_apagar(void *ctx) { potencia_apagar_todo(); }
static bool hw_arrancar(void *ctx, canal_t c, const parametros_t *p)
{
    return potencia_arrancar(c, p->f_ref_hz, p->fsw_base_hz);
}
static void hw_habilitar(void *ctx, canal_t c) { potencia_habilitar(c); }
static void hw_fref(void *ctx, uint32_t f) { potencia_fijar_fref(f); }
static bool hw_recuperar(void *ctx) { return potencia_recuperar_falla(); }

static const sm_hw_t s_hw = {
    .paro_activo = hw_paro,
    .canal_deshabilitado = hw_deshab,
    .apagar_todo = hw_apagar,
    .arrancar = hw_arrancar,
    .habilitar = hw_habilitar,
    .actualizar_fref = hw_fref,
    .recuperar_falla = hw_recuperar,
    .ctx = NULL,
};

/* ----------------------------------------------- Tarea de interfaz */

static void tarea_interfaz(lv_timer_t *t)
{
    evento_t ev;
    estado_t antes = s_sm.estado;
    while (xQueueReceive(s_cola, &ev, 0) == pdTRUE) {
        sm_evento(&s_sm, &ev, ahora_ms());
    }
    sm_tick(&s_sm, ahora_ms());

    if (s_sm.estado != antes) {
        ESP_LOGI(TAG, "%s -> %s", sm_nombre_estado(antes), sm_nombre_estado(s_sm.estado));
    }

    /* La calibración corre al entrar en ARRANQUE (también al reintentar) */
    if (s_sm.estado == EST_ARRANQUE) {
        if (!s_calib_pedida) {
            s_calib_pedida = true;
            medicion_calibrar();
        }
    } else {
        s_calib_pedida = false;
    }

    ui_datos_t d = {
        .sm = &s_sm,
        .ahora_ms = ahora_ms(),
        .paro = potencia_paro_activo(),
    };
    potencia_leer_estado(&d.pot);
    medicion_leer(&d.med);
    ui_actualizar(&d);
}

static void interfaz_lista(void)
{
    ui_iniciar(cb_ui);
    lv_timer_create(tarea_interfaz, PERIODO_INTERFAZ_MS, NULL);
}

/* --------------------------------------------------------- Arranque */

static SemaphoreHandle_t s_listo;
static esp_err_t s_res_potencia;

static void tarea_inicio_nucleo1(void *arg)
{
    s_res_potencia = potencia_iniciar(cb_paro_isr);
    xSemaphoreGive(s_listo);
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* 1. Drivers deshabilitados antes de tocar cualquier otro periférico */
    potencia_pines_seguros();

    s_cola = xQueueCreate(16, sizeof(evento_t));
    s_listo = xSemaphoreCreateBinary();

    /* 2. MCPWM detenidos y frenados, con la ISR en el núcleo 1 */
    xTaskCreatePinnedToCore(tarea_inicio_nucleo1, "inicio1", 4096, NULL, 5, NULL, 1);
    xSemaphoreTake(s_listo, portMAX_DELAY);
    ESP_ERROR_CHECK(s_res_potencia);

    /* 3. ADC1 continuo y protección en el núcleo 0 */
    ESP_ERROR_CHECK(medicion_iniciar(cb_disparo, cb_calib));

    /* 4. Máquina de estados en ARRANQUE; la calibración se pide en la
     *    primera pasada de la tarea de interfaz. */
    sm_iniciar(&s_sm, &s_hw, ahora_ms());

    /* 5. Pantalla y LVGL */
    ESP_ERROR_CHECK(pantalla_iniciar(interfaz_lista));

    /* 6. Solo en modo de banco: reporte por consola cada 2 s */
    banco_iniciar();
    ESP_LOGI(TAG, "firmware listo");
}
