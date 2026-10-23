/*
 * banco.c
 * Tarea que cada 2 s imprime por la consola USB:
 *   - ISR de la SPWM: llamadas por segundo, tiempo medio y máximo, periodo
 *     mínimo y máximo entre interrupciones, periodos perdidos.
 *   - Carga de cada núcleo (tareas, sin contar el tiempo de la ISR).
 *   - Muestras por segundo de cada sensor y tramas perdidas del DMA.
 *   - Tiempo desde que llega la trama del ADC hasta que se activa el freno,
 *     en el último disparo por sobrecorriente.
 *   - Veredicto de cada criterio de PRUEBA_BANCO.md.
 * La ISR marca su duración en PIN_DEPURACION para verla en el osciloscopio.
 */
#include "banco.h"

#if MODO_BANCO
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_clk_tree.h"
#include "potencia.h"
#include "medicion.h"

static const char *TAG = "banco";

#define PERIODO_REPORTE_MS 2000u

/* Criterios (ver PRUEBA_BANCO.md) */
#define ISR_MAX_FRACCION   0.25f   /* tiempo máximo de la ISR / periodo */
#define ADC_TOLERANCIA     0.05f
#define REACCION_MAX_US    2000

DRAM_ATTR banco_isr_t g_banco_isr[2];
DRAM_ATTR uint32_t g_banco_t_ant[2];

static volatile uint32_t s_muestras[SENSOR_NUM];
static volatile int64_t s_t_trama_us;
static volatile int64_t s_reaccion_us = -1;
static volatile uint32_t s_disparos;

void banco_adc_muestra(int sensor)
{
    s_muestras[sensor]++;
}

void IRAM_ATTR banco_adc_trama_isr(void)
{
    s_t_trama_us = esp_timer_get_time();
}

void banco_disparo(void)
{
    s_reaccion_us = esp_timer_get_time() - s_t_trama_us;
    s_disparos++;
}

/* Tiempo de ejecución acumulado de las tareas inactivas de cada núcleo */
static void inactividad(uint32_t idle[2])
{
    TaskStatus_t est[32];
    configRUN_TIME_COUNTER_TYPE total;
    UBaseType_t n = uxTaskGetSystemState(est, 32, &total);
    TaskHandle_t h0 = xTaskGetIdleTaskHandleForCore(0);
    TaskHandle_t h1 = xTaskGetIdleTaskHandleForCore(1);
    idle[0] = idle[1] = 0;
    for (UBaseType_t k = 0; k < n; k++) {
        if (est[k].xHandle == h0) idle[0] = est[k].ulRunTimeCounter;
        if (est[k].xHandle == h1) idle[1] = est[k].ulRunTimeCounter;
    }
}

static const char *veredicto(bool ok)
{
    return ok ? "OK" : "NO CUMPLE";
}

static void tarea_banco(void *arg)
{
    uint32_t f_cpu = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT, &f_cpu);
    const float us_por_ciclo = 1e6f / (float)f_cpu;

    uint32_t idle_ant[2];
    inactividad(idle_ant);
    uint32_t muestras_ant[SENSOR_NUM];
    memcpy(muestras_ant, (const void *)s_muestras, sizeof muestras_ant);
    int64_t t_ant = esp_timer_get_time();

    ESP_LOGW(TAG, "MODO DE BANCO: no conecte drivers ni potencia. CPU a %" PRIu32 " MHz, depuración en GPIO %d",
             f_cpu / 1000000, PIN_DEPURACION);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(PERIODO_REPORTE_MS));
        int64_t t = esp_timer_get_time();
        float dt = (float)(t - t_ant) * 1e-6f;
        t_ant = t;

        /* Copia y reinicio de los contadores de la ISR. La ISR corre en el
         * núcleo 1 y esta tarea en el 0, así que una llamada puede quedar
         * repartida entre dos reportes; eso no altera los máximos. */
        banco_isr_t isr[2];
        memcpy(isr, g_banco_isr, sizeof isr);
        memset(g_banco_isr, 0, sizeof g_banco_isr);

        potencia_estado_t pe;
        potencia_leer_estado(&pe);
        medicion_datos_t md;
        medicion_leer(&md);

        /* El contador de tiempo de ejecución de FreeRTOS en ESP-IDF cuenta en
         * microsegundos (esp_timer), igual que dt. */
        uint32_t idle[2];
        inactividad(idle);
        float carga[2];
        for (int c = 0; c < 2; c++) {
            float frac = (float)(idle[c] - idle_ant[c]) / (dt * 1e6f);
            carga[c] = 100.0f * (1.0f - frac);
            idle_ant[c] = idle[c];
        }

        ESP_LOGI(TAG, "---------------------------------------------------------------");
        ESP_LOGI(TAG, "Canal activo: %d   f1 = %.1f Hz   fsw = %.0f Hz   ma = %.3f",
                 (int)pe.canal, pe.f1_hz, pe.fsw_hz, pe.ma);

        bool ok_isr = true, ok_perdidos = true, ok_tasa = true;
        int g = (pe.canal == CANAL_1) ? 0 : (pe.canal == CANAL_2) ? 1 : -1;
        if (g >= 0 && isr[g].llamadas > 0) {
            banco_isr_t *b = &isr[g];
            float tasa = b->llamadas / dt;
            float t_med = (float)b->ciclos_suma / b->llamadas * us_por_ciclo;
            float t_max = b->ciclos_max * us_por_ciclo;
            float periodo_us = 1e6f / pe.fsw_hz;
            ok_isr = t_max <= ISR_MAX_FRACCION * periodo_us;
            ok_perdidos = (b->periodo_max * us_por_ciclo) < 1.5f * periodo_us;
            ok_tasa = tasa > 0.98f * pe.fsw_hz && tasa < 1.02f * pe.fsw_hz;
            if (!pe.rampa_completa) {
                ESP_LOGI(TAG, "ISR: rampa en curso, fsw cambia: espere a que termine para el veredicto");
            }
            ESP_LOGI(TAG, "ISR: %.0f llamadas/s (fsw %.0f) %s", tasa, pe.fsw_hz,
                     pe.rampa_completa ? veredicto(ok_tasa) : "-");
            ESP_LOGI(TAG, "ISR: tiempo medio %.2f us, máximo %.2f us de %.1f us de periodo (%.0f %%) %s",
                     t_med, t_max, periodo_us, 100.0f * t_max / periodo_us, veredicto(ok_isr));
            ESP_LOGI(TAG, "ISR: periodo entre llamadas de %.2f us a %.2f us %s",
                     b->periodo_min * us_por_ciclo, b->periodo_max * us_por_ciclo, veredicto(ok_perdidos));
            ESP_LOGI(TAG, "ISR: carga del núcleo 1 por la ISR %.1f %%",
                     100.0f * (float)b->ciclos_suma * us_por_ciclo * 1e-6f / dt);
        } else {
            ESP_LOGI(TAG, "ISR: sin canal modulando (inicie un canal desde la pantalla)");
        }

        ESP_LOGI(TAG, "Carga de tareas: núcleo 0 %.1f %%, núcleo 1 %.1f %% (sin contar ISR)", carga[0], carga[1]);

        bool ok_adc = true;
        char linea[96];
        int n = 0;
        for (int s = 0; s < SENSOR_NUM; s++) {
            uint32_t m = s_muestras[s];
            float tasa = (m - muestras_ant[s]) / dt;
            muestras_ant[s] = m;
            ok_adc = ok_adc && fabsf(tasa - ADC_FS_CANAL_HZ) <= ADC_TOLERANCIA * ADC_FS_CANAL_HZ;
            n += snprintf(linea + n, sizeof linea - n, " %.0f", tasa);
        }
        ok_adc = ok_adc && md.desbordes == 0;
        ESP_LOGI(TAG, "ADC: muestras/s CH1 U V =%s (esperado %u), tramas perdidas %" PRIu32 " %s",
                 linea, (unsigned)ADC_FS_CANAL_HZ, md.desbordes, veredicto(ok_adc));
        ESP_LOGI(TAG, "ADC: offset %d %d %d mV, I_RMS CH1 %.3f A, U %.3f A, V %.3f A, W %.3f A",
                 md.offset_mv[0], md.offset_mv[1], md.offset_mv[2],
                 md.rms_a[0], md.rms_a[1], md.rms_a[2], md.rms_a[3]);

        if (s_disparos > 0) {
            int64_t r = s_reaccion_us;
            ESP_LOGI(TAG, "Protección: %" PRIu32 " disparos, último: trama -> freno %" PRId64 " us %s",
                     s_disparos, r, veredicto(r >= 0 && r < REACCION_MAX_US));
        }
        ESP_LOGI(TAG, "Memoria interna libre %u B, PSRAM libre %u B",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}

void banco_iniciar(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_DEPURACION,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(PIN_DEPURACION, 0);
    xTaskCreatePinnedToCore(tarea_banco, "banco", 6144, NULL, 1, NULL, 0);
}
#endif
