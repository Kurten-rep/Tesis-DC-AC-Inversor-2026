/*
 * potencia.c
 *
 * Cada rama usa un operador del MCPWM: el generador A maneja HIN y el
 * generador B maneja LIN. B sale del submódulo de tiempo muerto a partir de
 * A, invertido, así que las dos señales nunca quedan en alto a la vez.
 *
 * Cadena del hardware: generador -> tiempo muerto -> portadora -> manejo de
 * fallas. El freno por falla actúa al final de la cadena y lleva HIN y LIN a
 * nivel bajo, sin importar el estado del generador ni del tiempo muerto.
 *
 * Fallas de cada grupo:
 *   - GPIO 7 (paro), activa en alto, freno de una sola vez (OST).
 *   - Una falla por software por rama (ESP-IDF no deja compartirla entre
 *     operadores), usada por la protección de sobrecorriente y por el
 *     apagado normal. También OST.
 * Mientras un canal no esté operando, su grupo queda frenado.
 *
 * Temporizadores en modo ascendente/descendente. En cada evento TEZ (cuenta
 * en cero) la ISR calcula los comparadores del siguiente periodo; el
 * hardware los carga en el TEZ siguiente (update_cmp_on_tez), de modo que no
 * hay pulsos incompletos.
 *
 * Glosario: OST = freno "one shot", queda activo hasta que se libera por
 * software. TEZ = "timer equals zero", el instante en que el contador del
 * temporizador pasa por cero. HIN / LIN = entradas de compuerta alta y baja
 * del driver. Grupo 0 = Canal 1, grupo 1 = Canal 2.
 */
#include "potencia.h"
#include "pines.h"
#include "config.h"
#include "spwm.h"
#include "banco.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "potencia";

#define G_CH1 0                     /* índice del grupo MCPWM del Canal 1 */
#define G_CH2 1                     /* índice del grupo MCPWM del Canal 2 */
#define TERCIO_VUELTA 1431655765u   /* 2^32 / 3: desfase de 120° en el acumulador de ángulo */
#define DTHETA_SINCRONO 204522252u  /* 2^32 / 21: avance de 360°/21 por periodo */

/* Todos los recursos del MCPWM de un canal. Se reservan hasta 3 ramas
 * (el Canal 1 usa 2 y el Canal 2 usa 3). */
typedef struct {
    int n_ramas;                      /* ramas usadas: 2 o 3 */
    mcpwm_timer_handle_t timer;       /* temporizador del grupo */
    mcpwm_oper_handle_t oper[3];      /* un operador por rama */
    mcpwm_cmpr_handle_t cmp[3];       /* comparador de cada rama */
    mcpwm_gen_handle_t gen_h[3];      /* generador A: señal HIN */
    mcpwm_gen_handle_t gen_l[3];      /* generador B: señal LIN */
    mcpwm_fault_handle_t f_paro;      /* falla por GPIO (paro de emergencia) */
    mcpwm_fault_handle_t f_soft[3];   /* ESP-IDF asigna cada falla por software a un solo operador */
    bool corriendo;                   /* true si el temporizador está en marcha */
} grupo_t;

static grupo_t s_g[2];   /* [0] = Canal 1, [1] = Canal 2 */

/* Referencias publicadas por la tarea de rampa: doble buffer por grupo.
 * La tarea escribe el buffer que la ISR no usa y luego cambia el índice. Las
 * dos corren en el núcleo 1, así que la ISR nunca ve un buffer a medias. */
static DRAM_ATTR spwm_ref_t s_ref[2][2];            /* [grupo][buffer] */
static DRAM_ATTR volatile uint32_t s_idx[2];        /* buffer que la ISR está usando */
static DRAM_ATTR uint32_t s_theta[2];               /* ángulo acumulado de cada grupo */

static volatile canal_t s_activo = CANAL_NINGUNO;   /* canal que está modulando */
static potencia_cb_paro_t s_cb_paro;                /* aviso hacia main.c cuando se activa el paro */

/* Estado de la rampa (solo lo toca la tarea de rampa, salvo f_ref) */
static volatile float s_f_ref_hz = CH2_F1_DEFECTO_HZ;   /* frecuencia pedida por el usuario */
static float s_fsw_base_hz = CH2_FSW_BASE_DEFECTO;      /* base de fsw elegida (10 o 20 kHz) */
static volatile bool s_reiniciar_rampa;                 /* pide a la rampa volver a cero */
static potencia_estado_t s_estado;                      /* última instantánea publicada */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;   /* protege s_estado */

/* ------------------------------------------------------------- EN y paro */

/* Lleva las dos líneas EN al nivel inactivo. Versión para ISR: en IRAM. */
static inline IRAM_ATTR void en_apagar_isr(void)
{
    gpio_set_level(PIN_EN_CH1, !EN_NIVEL_ACTIVO);
    gpio_set_level(PIN_EN_CH2, !EN_NIVEL_ACTIVO);
}

/* Es lo primero que corre en app_main(): deja los drivers deshabilitados
 * antes de configurar cualquier otro periférico. */
void potencia_pines_seguros(void)
{
    /* Primero el nivel, después la dirección, para no generar un pulso */
    gpio_set_level(PIN_EN_CH1, !EN_NIVEL_ACTIVO);
    gpio_set_level(PIN_EN_CH2, !EN_NIVEL_ACTIVO);
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_EN_CH1) | (1ULL << PIN_EN_CH2),
        .mode = GPIO_MODE_INPUT_OUTPUT,   /* se puede leer el nivel aplicado */
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(PIN_EN_CH1, !EN_NIVEL_ACTIVO);
    gpio_set_level(PIN_EN_CH2, !EN_NIVEL_ACTIVO);
}

/* El paro está activo con el pin en alto (NC abierto o cable cortado). */
bool potencia_paro_activo(void)
{
    return gpio_get_level(PIN_PARO) == 1;
}

/* true si el EN del canal está en nivel inactivo. Se lee el nivel real del
 * pin (por eso EN se configuró como entrada y salida). */
bool potencia_canal_deshabilitado(canal_t c)
{
    int pin = (c == CANAL_1) ? PIN_EN_CH1 : PIN_EN_CH2;
    return gpio_get_level(pin) != EN_NIVEL_ACTIVO;
}

canal_t potencia_canal_activo(void)
{
    return s_activo;
}

/* ------------------------------------------------------------------ ISR */

/* ISR del Canal 1, una vez por periodo de conmutación (evento TEZ). */
static IRAM_ATTR bool isr_ch1(mcpwm_timer_handle_t t, const mcpwm_timer_event_data_t *ed, void *ctx)
{
    uint32_t t0 = banco_isr_entrada(G_CH1);
    /* Copia local de la referencia vigente */
    spwm_ref_t r = s_ref[G_CH1][s_idx[G_CH1]];
    /* Avanza el ángulo y busca el seno en la tabla */
    s_theta[G_CH1] += r.dtheta;
    int32_t s = spwm_seno_q15(s_theta[G_CH1]);
    /* SPWM unipolar: las dos ramas con moduladoras en contrafase */
    mcpwm_comparator_set_compare_value(s_g[G_CH1].cmp[0], spwm_cmp(&r, s));
    mcpwm_comparator_set_compare_value(s_g[G_CH1].cmp[1], spwm_cmp(&r, -s));
    banco_isr_salida(G_CH1, t0);
    return false;
}

/* ISR del Canal 2. Además de los tres comparadores, ajusta el periodo del
 * temporizador, porque la frecuencia de conmutación varía con f1. */
static IRAM_ATTR bool isr_ch2(mcpwm_timer_handle_t t, const mcpwm_timer_event_data_t *ed, void *ctx)
{
    uint32_t t0 = banco_isr_entrada(G_CH2);
    spwm_ref_t r = s_ref[G_CH2][s_idx[G_CH2]];
    uint32_t th = (s_theta[G_CH2] += r.dtheta);
    mcpwm_timer_set_period(t, 2u * r.pico);   /* se aplica en el TEZ siguiente */
    /* Tres moduladoras desfasadas 120°: U en th, V en th - 120°, W en th + 120° */
    mcpwm_comparator_set_compare_value(s_g[G_CH2].cmp[0], spwm_cmp(&r, spwm_seno_q15(th)));
    mcpwm_comparator_set_compare_value(s_g[G_CH2].cmp[1], spwm_cmp(&r, spwm_seno_q15(th - TERCIO_VUELTA)));
    mcpwm_comparator_set_compare_value(s_g[G_CH2].cmp[2], spwm_cmp(&r, spwm_seno_q15(th + TERCIO_VUELTA)));
    banco_isr_salida(G_CH2, t0);
    return false;
}

/* ISR del paro de emergencia (flanco de entrada a la falla). */
static IRAM_ATTR bool isr_paro(mcpwm_fault_handle_t f, const mcpwm_fault_event_data_t *ed, void *ctx)
{
    /* Las salidas ya están en bajo por hardware. Se apaga EN y se avisa. */
    en_apagar_isr();
    s_activo = CANAL_NINGUNO;
    return s_cb_paro ? s_cb_paro() : false;
}

/* Publica una referencia nueva para la ISR del grupo g: escribe en el
 * buffer libre y después cambia el índice. */
static void publicar(int g, uint32_t pico, uint32_t dtheta, int32_t m_q15)
{
    uint32_t sig = s_idx[g] ^ 1u;   /* el buffer que la ISR no está usando */
    s_ref[g][sig].pico = pico;
    s_ref[g][sig].dtheta = dtheta;
    s_ref[g][sig].m_q15 = m_q15;
    s_ref[g][sig].cmp_max = spwm_cmp_max(pico, CICLO_MAX_PERMIL);
    /* Barrera: el compilador no puede reordenar la escritura del índice
     * antes que la de los campos */
    __asm__ __volatile__("" ::: "memory");
    s_idx[g] = sig;
}

/* -------------------------------------------------------- Configuración */

/* Crea todos los recursos del MCPWM de un canal: temporizador, falla del
 * paro, y para cada rama un operador, comparador, dos generadores, tiempo
 * muerto y freno. Deja el grupo frenado y detenido.
 *   g          grupo MCPWM (0 o 1)
 *   n_ramas    2 para el Canal 1, 3 para el Canal 2
 *   pin_h/l    GPIO de HIN y LIN de cada rama
 *   tm_cuentas tiempo muerto en cuentas de 12,5 ns
 *   pico_inicial  pico del triángulo con la fsw inicial
 *   isr        función que se llama en cada TEZ */
static esp_err_t crear_grupo(int g, int n_ramas, const int *pin_h, const int *pin_l,
                             uint32_t tm_cuentas, uint32_t pico_inicial,
                             mcpwm_timer_event_cb_t isr)
{
    grupo_t *gr = &s_g[g];
    gr->n_ramas = n_ramas;

    /* Temporizador ascendente/descendente: periodo = 2 x pico cuentas */
    mcpwm_timer_config_t tcfg = {
        .group_id = g,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = MCPWM_RESOLUCION_HZ,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP_DOWN,
        .period_ticks = 2u * pico_inicial,
        .flags.update_period_on_empty = true,
    };
    ESP_RETURN_ON_ERROR(mcpwm_new_timer(&tcfg, &gr->timer), TAG, "timer %d", g);

    /* Entrada de falla por GPIO: el pin de paro */
    mcpwm_gpio_fault_config_t fcfg = {
        .group_id = g,
        .gpio_num = PIN_PARO,
        .flags.active_level = 1,   /* pull-up externo: presionado = alto */
        /* Pull-up interno además del externo: sin cable (o sin la
         * resistencia externa montada) el pin queda en alto = paro, en vez
         * de flotar y generar una ráfaga de interrupciones. */
        .flags.pull_up = true,
    };
    ESP_RETURN_ON_ERROR(mcpwm_new_gpio_fault(&fcfg, &gr->f_paro), TAG, "falla gpio %d", g);

    for (int k = 0; k < n_ramas; k++) {
        /* Operador de la rama k, conectado al temporizador del grupo */
        mcpwm_operator_config_t ocfg = {
            .group_id = g,
            .flags.update_gen_action_on_tez = true,
            .flags.update_dead_time_on_tez = true,
        };
        ESP_RETURN_ON_ERROR(mcpwm_new_operator(&ocfg, &gr->oper[k]), TAG, "oper");
        ESP_RETURN_ON_ERROR(mcpwm_operator_connect_timer(gr->oper[k], gr->timer), TAG, "conectar");
        /* Falla por software de esta rama (para frenar desde el código) */
        mcpwm_soft_fault_config_t scfg = {};
        ESP_RETURN_ON_ERROR(mcpwm_new_soft_fault(&scfg, &gr->f_soft[k]), TAG, "falla soft");

        /* Comparador: su valor se carga en el siguiente TEZ, nunca a mitad de periodo */
        mcpwm_comparator_config_t ccfg = { .flags.update_cmp_on_tez = true };
        ESP_RETURN_ON_ERROR(mcpwm_new_comparator(gr->oper[k], &ccfg, &gr->cmp[k]), TAG, "cmp");
        ESP_RETURN_ON_ERROR(mcpwm_comparator_set_compare_value(gr->cmp[k], pico_inicial / 2), TAG, "cmp0");

        /* Un generador por cada salida: A (HIN) y B (LIN) */
        mcpwm_generator_config_t gh = { .gen_gpio_num = pin_h[k] };
        mcpwm_generator_config_t gl = { .gen_gpio_num = pin_l[k] };
        ESP_RETURN_ON_ERROR(mcpwm_new_generator(gr->oper[k], &gh, &gr->gen_h[k]), TAG, "gen h");
        ESP_RETURN_ON_ERROR(mcpwm_new_generator(gr->oper[k], &gl, &gr->gen_l[k]), TAG, "gen l");

        /* Ciclo de trabajo de HIN = cmp/pico: baja al subir y sube al bajar */
        ESP_RETURN_ON_ERROR(mcpwm_generator_set_action_on_compare_event(gr->gen_h[k],
            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, gr->cmp[k], MCPWM_GEN_ACTION_LOW)), TAG, "acc");
        ESP_RETURN_ON_ERROR(mcpwm_generator_set_action_on_compare_event(gr->gen_h[k],
            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_DOWN, gr->cmp[k], MCPWM_GEN_ACTION_HIGH)), TAG, "acc");

        /* Tiempo muerto: retrasa el flanco de subida de HIN y el de LIN */
        mcpwm_dead_time_config_t dh = { .posedge_delay_ticks = tm_cuentas };
        ESP_RETURN_ON_ERROR(mcpwm_generator_set_dead_time(gr->gen_h[k], gr->gen_h[k], &dh), TAG, "tm h");
        mcpwm_dead_time_config_t dl = { .negedge_delay_ticks = tm_cuentas, .flags.invert_output = true };
        ESP_RETURN_ON_ERROR(mcpwm_generator_set_dead_time(gr->gen_h[k], gr->gen_l[k], &dl), TAG, "tm l");

        /* Freno: las dos fallas, de una sola vez, salidas en bajo */
        mcpwm_brake_config_t b1 = { .fault = gr->f_paro, .brake_mode = MCPWM_OPER_BRAKE_MODE_OST };
        mcpwm_brake_config_t b2 = { .fault = gr->f_soft[k], .brake_mode = MCPWM_OPER_BRAKE_MODE_OST };
        ESP_RETURN_ON_ERROR(mcpwm_operator_set_brake_on_fault(gr->oper[k], &b1), TAG, "freno");
        ESP_RETURN_ON_ERROR(mcpwm_operator_set_brake_on_fault(gr->oper[k], &b2), TAG, "freno");
        /* Acción del freno sobre HIN y LIN: nivel bajo, con el contador
         * subiendo o bajando */
        mcpwm_gen_handle_t gens[2] = { gr->gen_h[k], gr->gen_l[k] };
        for (int j = 0; j < 2; j++) {
            ESP_RETURN_ON_ERROR(mcpwm_generator_set_action_on_brake_event(gens[j],
                MCPWM_GEN_BRAKE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_OPER_BRAKE_MODE_OST, MCPWM_GEN_ACTION_LOW)), TAG, "freno acc");
            ESP_RETURN_ON_ERROR(mcpwm_generator_set_action_on_brake_event(gens[j],
                MCPWM_GEN_BRAKE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_DOWN, MCPWM_OPER_BRAKE_MODE_OST, MCPWM_GEN_ACTION_LOW)), TAG, "freno acc");
        }
    }

    /* Frenado desde el inicio */
    for (int k = 0; k < n_ramas; k++) {
        ESP_RETURN_ON_ERROR(mcpwm_soft_fault_activate(gr->f_soft[k]), TAG, "frenar");
    }

    /* La ISR se registra en el evento "contador en cero" y el temporizador
     * se habilita, pero todavía no se inicia la cuenta */
    mcpwm_timer_event_callbacks_t tcb = { .on_empty = isr };
    ESP_RETURN_ON_ERROR(mcpwm_timer_register_event_callbacks(gr->timer, &tcb, NULL), TAG, "cb timer");
    ESP_RETURN_ON_ERROR(mcpwm_timer_enable(gr->timer), TAG, "enable");
    gr->corriendo = false;
    return ESP_OK;
}

/* ------------------------------------------------------- Tarea de rampa */

/* Corre cada PERIODO_RAMPA_MS (1 ms) en el núcleo 1. Hace los cálculos en
 * coma flotante que la ISR no puede hacer (rampas, ley V/f, regla de fsw),
 * publica el resultado en enteros y guarda una instantánea para la pantalla. */
static void tarea_rampa(void *arg)
{
    /* Constantes del Canal 1: no cambian durante la operación */
    const uint32_t pico_ch1 = spwm_pico_desde_fsw(MCPWM_RESOLUCION_HZ, CH1_FSW_HZ);
    const uint32_t dtheta_ch1 = spwm_dtheta(CH1_F1_HZ, spwm_fsw_real(MCPWM_RESOLUCION_HZ, pico_ch1));
    /* Incremento de ma por cada paso de 1 ms para llegar a CH1_MA_FINAL en 1 s */
    const float paso_ma_ch1 = CH1_MA_FINAL * (float)PERIODO_RAMPA_MS / (float)CH1_RAMPA_MA_MS;
    /* Incremento de f1 por paso de 1 ms con la pendiente de aceleración del motor */
    const float paso_f_ch2 = CH2_PENDIENTE_HZ_S * (float)PERIODO_RAMPA_MS / 1000.0f;

    float ma = 0.0f, f1 = CH2_F1_MIN_HZ;
    TickType_t t = xTaskGetTickCount();

    for (;;) {
        /* Periodo exacto, sin acumular retraso */
        vTaskDelayUntil(&t, pdMS_TO_TICKS(PERIODO_RAMPA_MS));
        canal_t c = s_activo;
        potencia_estado_t e = { .canal = c };

        /* Un nuevo arranque vuelve la rampa al punto de partida */
        if (s_reiniciar_rampa) {
            s_reiniciar_rampa = false;
            ma = 0.0f;
            f1 = CH2_F1_MIN_HZ;
        }

        if (c == CANAL_1) {
            /* Canal 1: f1 y fsw fijas; solo sube ma de 0 a CH1_MA_FINAL */
            ma = rampa_paso(ma, CH1_MA_FINAL, paso_ma_ch1);
            publicar(G_CH1, pico_ch1, dtheta_ch1, spwm_q15(ma));
            e.f1_hz = CH1_F1_HZ;
            e.f_ref_hz = CH1_F1_HZ;
            e.fsw_hz = spwm_fsw_real(MCPWM_RESOLUCION_HZ, pico_ch1);
            e.ma = ma;
            e.rampa_completa = (ma >= CH1_MA_FINAL);
        } else if (c == CANAL_2) {
            /* Canal 2: f1 sube hacia la referencia con la pendiente fija;
             * ma y fsw se derivan de f1 */
            float fref = s_f_ref_hz;
            f1 = rampa_paso(f1, fref, paso_f_ch2);
            ma = vf_ma(f1, CH2_MA0, CH2_F1_MAX_HZ);
            float fsw = vf_fsw(f1, s_fsw_base_hz, CH2_MF_SINCRONO, CH2_FSW_MAX_HZ);
            uint32_t pico = spwm_pico_desde_fsw(MCPWM_RESOLUCION_HZ, fsw);
            float fsw_real = spwm_fsw_real(MCPWM_RESOLUCION_HZ, pico);
            /* Síncrono cuando 21 x f1 alcanza o supera la base de fsw */
            bool sinc = ((float)CH2_MF_SINCRONO * f1) >= s_fsw_base_hz;
            /* Modulación síncrona: 360°/21 por periodo, f1 = fsw/21 exacto */
            uint32_t dth = sinc ? DTHETA_SINCRONO : spwm_dtheta(f1, fsw_real);
            publicar(G_CH2, pico, dth, spwm_q15(ma));
            e.f1_hz = sinc ? fsw_real / (float)CH2_MF_SINCRONO : f1;
            e.f_ref_hz = fref;
            e.fsw_hz = fsw_real;
            e.ma = ma;
            e.sincrono = sinc;
            e.rampa_completa = (f1 == fref);
        }

        /* Se publica la instantánea para la pantalla y la protección */
        portENTER_CRITICAL(&s_mux);
        s_estado = e;
        portEXIT_CRITICAL(&s_mux);
    }
}

/* Prepara la tabla de seno, las referencias iniciales, los dos grupos de
 * MCPWM (detenidos y frenados), el aviso del paro y la tarea de rampa. */
esp_err_t potencia_iniciar(potencia_cb_paro_t cb_paro)
{
    s_cb_paro = cb_paro;
    spwm_iniciar_tabla();

    /* Referencia inicial de cada grupo: ma = 0 y sin giro del ángulo */
    uint32_t pico1 = spwm_pico_desde_fsw(MCPWM_RESOLUCION_HZ, CH1_FSW_HZ);
    uint32_t pico2 = spwm_pico_desde_fsw(MCPWM_RESOLUCION_HZ, CH2_FSW_BASE_DEFECTO);
    for (int g = 0; g < 2; g++) {
        uint32_t p = g ? pico2 : pico1;
        s_ref[g][0] = (spwm_ref_t){ .pico = p, .dtheta = 0, .m_q15 = 0, .cmp_max = spwm_cmp_max(p, CICLO_MAX_PERMIL) };
        s_ref[g][1] = s_ref[g][0];
        s_idx[g] = 0;
        s_theta[g] = 0;
    }

    /* Pines HIN y LIN de cada rama */
    const int h1[2] = { PIN_HIN_A, PIN_HIN_B }, l1[2] = { PIN_LIN_A, PIN_LIN_B };
    const int h2[3] = { PIN_HIN_U, PIN_HIN_V, PIN_HIN_W }, l2[3] = { PIN_LIN_U, PIN_LIN_V, PIN_LIN_W };
    ESP_RETURN_ON_ERROR(crear_grupo(G_CH1, 2, h1, l1, TM_CH1_CUENTAS, pico1, isr_ch1), TAG, "CH1");
    ESP_RETURN_ON_ERROR(crear_grupo(G_CH2, 3, h2, l2, TM_CH2_CUENTAS, pico2, isr_ch2), TAG, "CH2");

    /* Un solo aviso de paro basta: el GPIO 7 frena los dos grupos */
    mcpwm_fault_event_callbacks_t fcb = { .on_fault_enter = isr_paro };
    ESP_RETURN_ON_ERROR(mcpwm_fault_register_event_callbacks(s_g[G_CH1].f_paro, &fcb, NULL), TAG, "cb paro");

    /* Tarea de rampa en el núcleo 1, junto a las ISR */
    BaseType_t ok = xTaskCreatePinnedToCore(tarea_rampa, "rampa", 4096, NULL,
                                            configMAX_PRIORITIES - 2, NULL, 1);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "tarea rampa");
    ESP_LOGI(TAG, "MCPWM listo: CH1 %u cuentas de pico, CH2 %u", (unsigned)pico1, (unsigned)pico2);
    return ESP_OK;
}

/* ------------------------------------------------------------ Comandos */

/* Activa la falla por software de todas las ramas de los dos canales:
 * HIN y LIN pasan a nivel bajo por hardware. */
static void frenar_grupos(void)
{
    for (int g = 0; g < 2; g++) {
        for (int k = 0; k < s_g[g].n_ramas; k++) {
            mcpwm_soft_fault_activate(s_g[g].f_soft[k]);
        }
    }
}

/* Disparo de protección: lo más rápido posible, sin esperar nada. Primero
 * el freno de las salidas, luego EN inactivo, y se marca que no hay canal
 * activo. */
void potencia_disparo(void)
{
    frenar_grupos();
    gpio_set_level(PIN_EN_CH1, !EN_NIVEL_ACTIVO);
    gpio_set_level(PIN_EN_CH2, !EN_NIVEL_ACTIVO);
    s_activo = CANAL_NINGUNO;
}

/* Apagado completo: igual que el disparo, y además detiene los
 * temporizadores que estén corriendo. */
void potencia_apagar_todo(void)
{
    potencia_disparo();
    for (int g = 0; g < 2; g++) {
        if (s_g[g].corriendo) {
            mcpwm_timer_start_stop(s_g[g].timer, MCPWM_TIMER_STOP_EMPTY);
            s_g[g].corriendo = false;
        }
    }
}

/* Arranca el MCPWM del canal c con ma = 0 y libera el freno. Los drivers
 * siguen deshabilitados (EN inactivo) hasta potencia_habilitar(). */
bool potencia_arrancar(canal_t c, uint32_t f_ref_hz, uint32_t fsw_base_hz)
{
    if (c != CANAL_1 && c != CANAL_2) {
        return false;
    }
    int g = (c == CANAL_1) ? G_CH1 : G_CH2;
    grupo_t *gr = &s_g[g];

    /* Referencia inicial con ma = 0: las ramas quedan al 50 % y cargan los
     * condensadores bootstrap antes de que suba el voltaje de salida. */
    s_f_ref_hz = (float)f_ref_hz;
    s_fsw_base_hz = (float)fsw_base_hz;
    float fsw0 = (c == CANAL_1) ? (float)CH1_FSW_HZ : vf_fsw(CH2_F1_MIN_HZ, s_fsw_base_hz, CH2_MF_SINCRONO, CH2_FSW_MAX_HZ);
    uint32_t pico = spwm_pico_desde_fsw(MCPWM_RESOLUCION_HZ, fsw0);
    s_theta[g] = 0;
    /* Se publica dos veces para que los dos buffers tengan la referencia inicial */
    publicar(g, pico, 0, 0);
    publicar(g, pico, 0, 0);
    s_reiniciar_rampa = true;

    if (mcpwm_timer_set_period(gr->timer, 2u * pico) != ESP_OK) {
        return false;
    }
    banco_isr_reiniciar(g);
    if (mcpwm_timer_start_stop(gr->timer, MCPWM_TIMER_START_NO_STOP) != ESP_OK) {
        return false;
    }
    gr->corriendo = true;
    vTaskDelay(pdMS_TO_TICKS(2));   /* algunos periodos con el freno activo */

    /* Se libera el freno de cada rama; si falla, se deja todo apagado */
    for (int k = 0; k < gr->n_ramas; k++) {
        if (mcpwm_operator_recover_from_fault(gr->oper[k], gr->f_paro) != ESP_OK ||
            mcpwm_operator_recover_from_fault(gr->oper[k], gr->f_soft[k]) != ESP_OK) {
            ESP_LOGE(TAG, "no se pudo liberar el freno del grupo %d", g);
            potencia_apagar_todo();
            return false;
        }
    }
    s_activo = c;
    return true;
}

/* Activa EN del canal c y desactiva el otro. */
void potencia_habilitar(canal_t c)
{
    /* Nunca los dos: primero se apaga el otro */
    if (c == CANAL_1) {
        gpio_set_level(PIN_EN_CH2, !EN_NIVEL_ACTIVO);
        gpio_set_level(PIN_EN_CH1, EN_NIVEL_ACTIVO);
    } else if (c == CANAL_2) {
        gpio_set_level(PIN_EN_CH1, !EN_NIVEL_ACTIVO);
        gpio_set_level(PIN_EN_CH2, EN_NIVEL_ACTIVO);
    }
}

/* Cambia la frecuencia pedida; la tarea de rampa la sigue con su pendiente. */
void potencia_fijar_fref(uint32_t f_ref_hz)
{
    s_f_ref_hz = (float)f_ref_hz;
}

/* Se usa al reconocer una falla. Devuelve true si el paro ya está liberado. */
bool potencia_recuperar_falla(void)
{
    /* El freno se libera en potencia_arrancar; aquí solo se exige que el
     * paro esté liberado y que todo siga apagado. */
    potencia_apagar_todo();
    return !potencia_paro_activo();
}

/* Frecuencia de salida de la última instantánea publicada por la rampa. */
float potencia_f1_actual(void)
{
    portENTER_CRITICAL(&s_mux);
    float f = s_estado.f1_hz;
    portEXIT_CRITICAL(&s_mux);
    return f;
}

/* Copia la última instantánea de forma atómica. */
void potencia_leer_estado(potencia_estado_t *e)
{
    portENTER_CRITICAL(&s_mux);
    *e = s_estado;
    portEXIT_CRITICAL(&s_mux);
}
