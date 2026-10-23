/*
 * medicion.c
 *
 * El ADC1 convierte en secuencia I_CH1, I_U, I_V a 60 kS/s en total y deja
 * las muestras por DMA en tramas de 1 ms. La tarea de protección, en el
 * núcleo 0, procesa cada trama:
 *   1. raw -> mV con una tabla hecha en el arranque desde esp_adc_cali.
 *   2. mV -> A con el offset medido en el arranque.
 *   3. En el Canal 2, iW = -(iU + iV) con el último par U, V.
 *   4. |i| contra el umbral del canal activo. Si lo supera: freno por
 *      software, EN apagado y aviso a la máquina de estados.
 *   5. Suma de cuadrados para el valor eficaz sobre un número entero de
 *      periodos de la fundamental (ecuación 11).
 * El tiempo de reacción queda entre 1 y 2 tramas (1 ms a 2 ms) más el
 * procesamiento. [VERIFICAR] medirlo en la prueba de protecciones.
 */
#include "medicion.h"
#include "config.h"
#include "pines.h"
#include "potencia.h"
#include "banco.h"

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "medicion";

static adc_continuous_handle_t s_adc;
static TaskHandle_t s_tarea;
static uint16_t *s_mv;                 /* tabla raw -> mV, 4096 entradas */
static medicion_cb_disparo_t s_cb_disparo;
static medicion_cb_calib_t s_cb_calib;

static int s_offset_mv[SENSOR_NUM] = { 1667, 1667, 1667 };
static float s_mv_por_a[SENSOR_NUM];

static volatile bool s_pedir_calib;
static medicion_datos_t s_datos;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_desbordes;

static int sensor_de_canal_adc(int ch)
{
    switch (ch) {
    case ADC_CANAL_I_CH1: return SENSOR_CH1;
    case ADC_CANAL_I_U:   return SENSOR_U;
    case ADC_CANAL_I_V:   return SENSOR_V;
    default:              return -1;
    }
}

static bool IRAM_ATTR cb_trama(adc_continuous_handle_t h, const adc_continuous_evt_data_t *ev, void *ctx)
{
    BaseType_t despierta = pdFALSE;
    banco_adc_trama_isr();
    vTaskNotifyGiveFromISR(s_tarea, &despierta);
    return despierta == pdTRUE;
}

static bool IRAM_ATTR cb_desborde(adc_continuous_handle_t h, const adc_continuous_evt_data_t *ev, void *ctx)
{
    s_desbordes++;
    return false;
}

static esp_err_t crear_tabla_mv(void)
{
    adc_cali_handle_t cali = NULL;
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    s_mv = heap_caps_malloc(4096 * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_mv, ESP_ERR_NO_MEM, TAG, "tabla");
    esp_err_t r = adc_cali_create_scheme_curve_fitting(&cfg, &cali);
    for (int raw = 0; raw < 4096; raw++) {
        int mv = raw * 3100 / 4095;   /* aproximación si no hay eFuse de calibración */
        if (r == ESP_OK) {
            adc_cali_raw_to_voltage(cali, raw, &mv);
        }
        s_mv[raw] = (uint16_t)mv;
    }
    if (r == ESP_OK) {
        adc_cali_delete_scheme_curve_fitting(cali);
    } else {
        ESP_LOGW(TAG, "sin calibración de fábrica del ADC, se usa la recta nominal");
    }
    return ESP_OK;
}

/* Muestras por ventana de valor eficaz: un número entero de periodos de f1
 * que cubra al menos RMS_VENTANA_MIN_S. */
static uint32_t muestras_ventana(float f1_hz)
{
    if (f1_hz < 1.0f) {
        return (uint32_t)(RMS_VENTANA_MIN_S * ADC_FS_CANAL_HZ);
    }
    float periodos = ceilf(RMS_VENTANA_MIN_S * f1_hz);
    return (uint32_t)lroundf(periodos * (float)ADC_FS_CANAL_HZ / f1_hz);
}

static void tarea_proteccion(void *arg)
{
    static uint8_t buf[ADC_BYTES_TRAMA];

    /* Valor eficaz: índices 0..2 sensores, 3 = W calculada */
    float suma2[4] = { 0 };   /* float: el S3 no tiene FPU de doble precisión */
    uint32_t n[4] = { 0 };
    uint32_t n_ventana = muestras_ventana(0);

    /* Calibración */
    bool calibrando = false;
    int64_t suma_cal[SENSOR_NUM] = { 0 };
    uint32_t n_cal[SENSOR_NUM] = { 0 };
    const uint32_t n_cal_obj = (CALIBRACION_MS * ADC_FS_CANAL_HZ) / 1000u;

    float iu = 0, iv = 0;
    bool hay_u = false, hay_v = false;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));

        if (s_pedir_calib) {
            s_pedir_calib = false;
            calibrando = true;
            memset(suma_cal, 0, sizeof suma_cal);
            memset(n_cal, 0, sizeof n_cal);
        }

        uint32_t leidos = 0;
        while (adc_continuous_read(s_adc, buf, sizeof buf, &leidos, 0) == ESP_OK) {
            canal_t activo = potencia_canal_activo();

            for (uint32_t k = 0; k + SOC_ADC_DIGI_RESULT_BYTES <= leidos; k += SOC_ADC_DIGI_RESULT_BYTES) {
                adc_digi_output_data_t *d = (adc_digi_output_data_t *)&buf[k];
                if (d->type2.unit != 0) {
                    continue;
                }
                int s = sensor_de_canal_adc(d->type2.channel);
                if (s < 0) {
                    continue;
                }
                int mv = s_mv[d->type2.data & 0x0FFF];
                banco_adc_muestra(s);

                if (calibrando) {
                    suma_cal[s] += mv;
                    n_cal[s]++;
                    continue;
                }

                float i = (float)(mv - s_offset_mv[s]) / s_mv_por_a[s];
                suma2[s] += i * i;
                n[s]++;

                if (s == SENSOR_U) { iu = i; hay_u = true; }
                if (s == SENSOR_V) { iv = i; hay_v = true; }

                if (activo == CANAL_1 && s == SENSOR_CH1 && fabsf(i) > CH1_UMBRAL_A) {
                    potencia_disparo();
                    banco_disparo();
                    s_cb_disparo(CAUSA_SOBRECORRIENTE_CH1, i, CH1_F1_HZ);
                    activo = CANAL_NINGUNO;
                } else if (activo == CANAL_2 && (s == SENSOR_U || s == SENSOR_V)) {
                    if (fabsf(i) > CH2_UMBRAL_A) {
                        float f1 = potencia_f1_actual();
                        potencia_disparo();
                        banco_disparo();
                        s_cb_disparo(s == SENSOR_U ? CAUSA_SOBRECORRIENTE_U : CAUSA_SOBRECORRIENTE_V, i, f1);
                        activo = CANAL_NINGUNO;
                    }
                }
                if (hay_u && hay_v) {
                    float iw = -(iu + iv);
                    hay_u = hay_v = false;
                    suma2[3] += iw * iw;
                    n[3]++;
                    if (activo == CANAL_2 && fabsf(iw) > CH2_UMBRAL_A) {
                        float f1 = potencia_f1_actual();
                        potencia_disparo();
                        banco_disparo();
                        s_cb_disparo(CAUSA_SOBRECORRIENTE_W, iw, f1);
                        activo = CANAL_NINGUNO;
                    }
                }
            }
        }

        if (calibrando && n_cal[0] >= n_cal_obj && n_cal[1] >= n_cal_obj && n_cal[2] >= n_cal_obj) {
            calibrando = false;
            bool ok = true;
            portENTER_CRITICAL(&s_mux);
            for (int s = 0; s < SENSOR_NUM; s++) {
                int off = (int)(suma_cal[s] / (int64_t)n_cal[s]);
                bool en_rango = (off >= OFFSET_MIN_MV && off <= OFFSET_MAX_MV);
                s_datos.offset_mv[s] = off;
                s_datos.offset_ok[s] = en_rango;
                if (en_rango) {
                    s_offset_mv[s] = off;
                }
                ok = ok && en_rango;
            }
            s_datos.calibrado = ok;
            portEXIT_CRITICAL(&s_mux);
            ESP_LOGI(TAG, "offsets: %d %d %d mV -> %s", s_datos.offset_mv[0],
                     s_datos.offset_mv[1], s_datos.offset_mv[2], ok ? "ok" : "fuera de rango");
            memset(suma2, 0, sizeof suma2);
            memset(n, 0, sizeof n);
            s_cb_calib(ok);
        }

        /* Cierre de la ventana de valor eficaz, con la W como referencia */
        if (!calibrando && n[0] >= n_ventana) {
            float rms[4];
            for (int s = 0; s < 4; s++) {
                rms[s] = n[s] ? sqrtf(suma2[s] / (float)n[s]) : 0.0f;
                suma2[s] = 0.0f;
                n[s] = 0;
            }
            canal_t c = potencia_canal_activo();
            float f1 = (c == CANAL_1) ? (float)CH1_F1_HZ : (c == CANAL_2) ? potencia_f1_actual() : 0.0f;
            n_ventana = muestras_ventana(f1);
            portENTER_CRITICAL(&s_mux);
            memcpy(s_datos.rms_a, rms, sizeof rms);
            s_datos.desbordes = s_desbordes;
            portEXIT_CRITICAL(&s_mux);
        }
    }
}

esp_err_t medicion_iniciar(medicion_cb_disparo_t cb_disparo, medicion_cb_calib_t cb_calib)
{
    s_cb_disparo = cb_disparo;
    s_cb_calib = cb_calib;
    s_mv_por_a[SENSOR_CH1] = SENS_CH1_MV_A * DIVISOR_RELACION;
    s_mv_por_a[SENSOR_U] = SENS_CH2_MV_A * DIVISOR_RELACION;
    s_mv_por_a[SENSOR_V] = SENS_CH2_MV_A * DIVISOR_RELACION;

    ESP_RETURN_ON_ERROR(crear_tabla_mv(), TAG, "tabla mV");

    adc_continuous_handle_cfg_t hcfg = {
        .max_store_buf_size = ADC_BYTES_TRAMA * 8,
        .conv_frame_size = ADC_BYTES_TRAMA,
    };
    ESP_RETURN_ON_ERROR(adc_continuous_new_handle(&hcfg, &s_adc), TAG, "handle");

    adc_digi_pattern_config_t patron[ADC_NUM_CANALES] = {
        { .atten = ADC_ATTEN_DB_12, .channel = ADC_CANAL_I_CH1, .unit = ADC_UNIT_1, .bit_width = ADC_BITWIDTH_12 },
        { .atten = ADC_ATTEN_DB_12, .channel = ADC_CANAL_I_U,   .unit = ADC_UNIT_1, .bit_width = ADC_BITWIDTH_12 },
        { .atten = ADC_ATTEN_DB_12, .channel = ADC_CANAL_I_V,   .unit = ADC_UNIT_1, .bit_width = ADC_BITWIDTH_12 },
    };
    adc_continuous_config_t ccfg = {
        .pattern_num = ADC_NUM_CANALES,
        .adc_pattern = patron,
        .sample_freq_hz = ADC_FS_TOTAL_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    ESP_RETURN_ON_ERROR(adc_continuous_config(s_adc, &ccfg), TAG, "config");

    BaseType_t ok = xTaskCreatePinnedToCore(tarea_proteccion, "proteccion", 4096, NULL,
                                            configMAX_PRIORITIES - 3, &s_tarea, 0);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "tarea");

    adc_continuous_evt_cbs_t cbs = { .on_conv_done = cb_trama, .on_pool_ovf = cb_desborde };
    ESP_RETURN_ON_ERROR(adc_continuous_register_event_callbacks(s_adc, &cbs, NULL), TAG, "cb");
    ESP_RETURN_ON_ERROR(adc_continuous_start(s_adc), TAG, "start");
    return ESP_OK;
}

void medicion_calibrar(void)
{
    portENTER_CRITICAL(&s_mux);
    s_datos.calibrado = false;
    portEXIT_CRITICAL(&s_mux);
    s_pedir_calib = true;
    xTaskNotifyGive(s_tarea);
}

void medicion_leer(medicion_datos_t *d)
{
    portENTER_CRITICAL(&s_mux);
    *d = s_datos;
    portEXIT_CRITICAL(&s_mux);
}
