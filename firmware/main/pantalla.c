/*
 * pantalla.c
 * Arranque del bus SPI2, de la pantalla ILI9341, del táctil XPT2046 y de
 * LVGL. Contenido, en orden:
 *   1. Secuencia de registros de inicio del ILI9341 y perfiles de color.
 *   2. Funciones de LVGL: reloj y envío de los píxeles a la pantalla.
 *   3. Lectura del táctil y conversión a coordenadas de pantalla.
 *   4. Tarea de LVGL y pantalla_iniciar().
 *
 * Los dos dispositivos SPI (pantalla y táctil) comparten el mismo bus y se
 * distinguen por su línea CS.
 */
#include "pantalla.h"
#include "config.h"
#include "pines.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char *TAG = "pantalla";

#define SPI_BUS SPI2_HOST                      /* controlador SPI que se usa */
#define BUFFER_PX (LCD_H_RES * LCD_LINEAS_BUFFER)   /* píxeles por buffer de dibujo */

static esp_lcd_panel_handle_t s_panel;   /* manejador de la pantalla ILI9341 */
static spi_device_handle_t s_tactil;     /* manejador del XPT2046 en el bus SPI */
static lv_display_t *s_disp;             /* pantalla de LVGL */
static pantalla_cb_listo_t s_cb_listo;   /* función que construye la interfaz */

#ifndef LCD_PERFIL
#define LCD_PERFIL 1   /* por si config.h no lo define */
#endif

/* Secuencia de inicio: temporización y barrido del driver de Espressif,
 * más la potencia, el VCOM y la gamma del perfil elegido en config.h.
 *
 * Cada entrada tiene la forma { registro, {parámetros}, cantidad, retardo }.
 * INIT_TIEMPOS es común a todos los perfiles: no cambia los colores. Los
 * valores de fábrica de estos registros dejaban la imagen entrelazada, por
 * eso se reemplazan por los del driver de Espressif. */
#define INIT_TIEMPOS \
    {0xCF, (uint8_t[]){0x00, 0xAA, 0xE0}, 3, 0},             /* Power control B */ \
    {0xED, (uint8_t[]){0x67, 0x03, 0x12, 0x81}, 4, 0},       /* Power on sequence */ \
    {0xE8, (uint8_t[]){0x8A, 0x01, 0x78}, 3, 0},             /* Driver timing A */ \
    {0xCB, (uint8_t[]){0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0}, /* Power control A */ \
    {0xF7, (uint8_t[]){0x20}, 1, 0},                         /* Pump ratio */ \
    {0xEA, (uint8_t[]){0x00, 0x00}, 2, 0},                   /* Driver timing B */ \
    {0xB1, (uint8_t[]){0x00, 0x1B}, 2, 0},                   /* Frame rate 70 Hz */ \
    {0xF2, (uint8_t[]){0x00}, 1, 0},                         /* 3G desactivado */ \
    {0x26, (uint8_t[]){0x01}, 1, 0},                         /* Curva de gamma 1 */ \
    {0xB7, (uint8_t[]){0x07}, 1, 0},                         /* Entry mode */ \
    {0xB6, (uint8_t[]){0x08, 0x82, 0x27}, 3, 0}              /* Display function */

/* Tablas de gamma (registros 0xE0 positivo y 0xE1 negativo, 15 valores cada
 * una). Definen la curva de brillo de cada nivel de color; cambiarlas
 * modifica la saturación y el contraste aparentes. */
#define GAMMA_ESPRESSIF \
    {0xE0, (uint8_t[]){0x1F, 0x36, 0x36, 0x3A, 0x0C, 0x05, 0x4F, 0x87, 0x3C, 0x08, 0x11, 0x35, 0x19, 0x13, 0x00}, 15, 0}, \
    {0xE1, (uint8_t[]){0x00, 0x09, 0x09, 0x05, 0x13, 0x0A, 0x30, 0x78, 0x43, 0x07, 0x0E, 0x0A, 0x26, 0x2C, 0x1F}, 15, 0}
#define GAMMA_ADAFRUIT \
    {0xE0, (uint8_t[]){0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 15, 0}, \
    {0xE1, (uint8_t[]){0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 15, 0}
#define GAMMA_IDF \
    {0xE0, (uint8_t[]){0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0x87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00}, 15, 0}, \
    {0xE1, (uint8_t[]){0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F}, 15, 0}

/* Perfil de color según LCD_PERFIL (config.h). Cada perfil fija:
 *   0xC0 / 0xC1  potencia (tensión de los amplificadores de la fuente)
 *   0xC5 / 0xC7  VCOM (tensión de contraste del panel)
 *   0xE0 / 0xE1  gamma
 * Perfiles: 1 Espressif, 2 Adafruit, 3 ESP-IDF antiguo, 5 Adafruit con VCOM
 * alto, 6 ESP-IDF antiguo con VCOM alto. */
#if LCD_PERFIL == 2
#define INIT_PERFIL {0xC0, (uint8_t[]){0x23}, 1, 0}, {0xC1, (uint8_t[]){0x10}, 1, 0}, \
    {0xC5, (uint8_t[]){0x3E, 0x28}, 2, 0}, {0xC7, (uint8_t[]){0x86}, 1, 0}, GAMMA_ADAFRUIT
#elif LCD_PERFIL == 3
#define INIT_PERFIL {0xC0, (uint8_t[]){0x26}, 1, 0}, {0xC1, (uint8_t[]){0x11}, 1, 0}, \
    {0xC5, (uint8_t[]){0x35, 0x3E}, 2, 0}, {0xC7, (uint8_t[]){0xBE}, 1, 0}, GAMMA_IDF
#elif LCD_PERFIL == 5
#define INIT_PERFIL {0xC0, (uint8_t[]){0x23}, 1, 0}, {0xC1, (uint8_t[]){0x10}, 1, 0}, \
    {0xC5, (uint8_t[]){0x3E, 0x28}, 2, 0}, {0xC7, (uint8_t[]){0xA0}, 1, 0}, GAMMA_ADAFRUIT
#elif LCD_PERFIL == 6
#define INIT_PERFIL {0xC0, (uint8_t[]){0x26}, 1, 0}, {0xC1, (uint8_t[]){0x11}, 1, 0}, \
    {0xC5, (uint8_t[]){0x35, 0x3E}, 2, 0}, {0xC7, (uint8_t[]){0xD0}, 1, 0}, GAMMA_IDF
#else   /* 1: Espressif */
#define INIT_PERFIL {0xC0, (uint8_t[]){0x23}, 1, 0}, {0xC1, (uint8_t[]){0x11}, 1, 0}, \
    {0xC5, (uint8_t[]){0x43, 0x4C}, 2, 0}, {0xC7, (uint8_t[]){0xA0}, 1, 0}, GAMMA_ESPRESSIF
#endif

/* Lista completa de comandos que el driver envía al panel al iniciarlo, y
 * la estructura que la entrega al driver (vendor_config). */
static const ili9341_lcd_init_cmd_t s_init_panel[] = { INIT_TIEMPOS, INIT_PERFIL };
static const ili9341_vendor_config_t s_vendor_panel = {
    .init_cmds = s_init_panel,
    .init_cmds_size = sizeof(s_init_panel) / sizeof(s_init_panel[0]),
};

/* --------------------------------------------------------------- LVGL */

/* Reloj de LVGL: milisegundos desde el arranque. */
static uint32_t reloj_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Se ejecuta en una ISR cuando el DMA termina de enviar un bloque de
 * píxeles. Avisa a LVGL para que pueda reutilizar el buffer. */
static bool IRAM_ATTR fin_envio(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ed, void *ctx)
{
    lv_display_flush_ready((lv_display_t *)ctx);
    return false;
}

/* LVGL llama aquí con el área redibujada ("a") y sus píxeles ("px"); se
 * envían a la pantalla por SPI con DMA. */
static void volcar(lv_display_t *disp, const lv_area_t *a, uint8_t *px)
{
    /* El ILI9341 espera RGB565 con el byte alto primero */
    lv_draw_sw_rgb565_swap(px, lv_area_get_size(a));
    esp_lcd_panel_draw_bitmap(s_panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
}

/* ------------------------------------------------------------- XPT2046 */

/* Envía un byte de comando al XPT2046 y devuelve la conversión de 12 bits.
 * La transacción tiene 24 bits: 8 de comando y 16 de respuesta, de los
 * cuales los 12 más significativos son el resultado. */
static uint16_t xpt_leer(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 24,
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .tx_data = { cmd, 0, 0, 0 },
    };
    if (spi_device_polling_transmit(s_tactil, &t) != ESP_OK) {
        return 0;
    }
    return (uint16_t)(((t.rx_data[1] << 8) | t.rx_data[2]) >> 3) & 0x0FFF;
}

/* Convierte una lectura cruda "v" del rango [vmin, vmax] a una coordenada
 * en [0, salida_max - 1], con recorte en los bordes y opción de invertir el
 * eje. */
static int mapear(int v, int vmin, int vmax, int salida_max, bool invertir)
{
    int r = (v - vmin) * salida_max / (vmax - vmin);
    if (r < 0) r = 0;
    if (r > salida_max - 1) r = salida_max - 1;
    return invertir ? (salida_max - 1 - r) : r;
}

/* LVGL la llama periódicamente para saber si hay un toque y dónde. */
static void leer_tactil(lv_indev_t *indev, lv_indev_data_t *data)
{
    static int32_t ult_x, ult_y;   /* último punto válido, que se repite al soltar */
    /* Presión: Z1 = 0xB1, Z2 = 0xC1. Con PD = 01 el ADC queda encendido
     * entre lecturas y el IRQ desactivado, lo que da lecturas estables. */
    int z1 = (int)xpt_leer(0xB1);
    int z2 = (int)xpt_leer(0xC1);
    int z = z1 + 4095 - z2;   /* medida de presión: crece al apretar */
    /* Presión insuficiente: no hay toque, se informa "soltado" en el último punto */
    if (z1 < 30 || z < TACTIL_Z_UMBRAL) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = ult_x;
        data->point.y = ult_y;
        return;
    }
    /* Promedio de 4 lecturas; la primera de cada eje se descarta
     * (0x91 pide el eje Y, 0xD1 el eje X) */
    int sx = 0, sy = 0;
    xpt_leer(0x91);
    for (int k = 0; k < 4; k++) {
        sy += xpt_leer(0x91);
        sx += xpt_leer(0xD1);
    }
    xpt_leer(0xD0);   /* deja el XPT2046 en reposo con IRQ activo */
    int rx = sx / 4, ry = sy / 4;
#if TACTIL_DEPURAR
    /* Valores crudos para calibrar TACTIL_X_MIN / MAX y TACTIL_Y_MIN / MAX */
    ESP_LOGI(TAG, "tactil crudo x=%d y=%d z=%d", rx, ry, z);
#endif
#if TACTIL_INTERCAMBIAR_XY
    int tmp = rx; rx = ry; ry = tmp;
#endif
    /* Conversión a píxeles de la pantalla */
    ult_x = mapear(rx, TACTIL_X_MIN, TACTIL_X_MAX, LCD_H_RES, TACTIL_INVERTIR_X);
    ult_y = mapear(ry, TACTIL_Y_MIN, TACTIL_Y_MAX, LCD_V_RES, TACTIL_INVERTIR_Y);
    data->point.x = ult_x;
    data->point.y = ult_y;
    data->state = LV_INDEV_STATE_PRESSED;
}

/* --------------------------------------------------------------- Tarea */

/* Única tarea que llama a LVGL. Primero construye la interfaz (cb_listo) y
 * después atiende los temporizadores de LVGL en bucle. */
static void tarea_lvgl(void *arg)
{
    if (s_cb_listo) {
        s_cb_listo();
    }
    for (;;) {
        /* lv_timer_handler() devuelve cuántos ms puede dormir; se limita
         * entre 2 y 20 ms para mantener la respuesta del táctil */
        uint32_t espera = lv_timer_handler();
        if (espera > 20) espera = 20;
        if (espera < 2) espera = 2;
        vTaskDelay(pdMS_TO_TICKS(espera));
    }
}

/* Inicializa el bus SPI, la pantalla, el táctil y LVGL, y lanza la tarea de
 * LVGL en el núcleo 0. */
esp_err_t pantalla_iniciar(pantalla_cb_listo_t cb_listo)
{
    s_cb_listo = cb_listo;

    /* Bus SPI2 compartido por pantalla y táctil */
    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SPI_SCK,
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BUFFER_PX * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI_BUS, &bus, SPI_DMA_CH_AUTO), TAG, "bus SPI");

    /* LVGL: reloj y objeto de pantalla de 320 x 240 */
    lv_init();
    lv_tick_set_cb(reloj_ms);
    s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);

    /* Canal SPI hacia el ILI9341 (líneas CS y DC, reloj, cola de envío) */
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_TFT_CS,
        .dc_gpio_num = PIN_TFT_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_SPI_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = fin_envio,
        .user_ctx = s_disp,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI_BUS, &io_cfg, &io), TAG, "io LCD");

    /* Panel ILI9341 con la secuencia de inicio de este archivo */
    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = -1,   /* TFT_RST va al EN de la placa */
#if LCD_ORDEN_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
        .vendor_config = (void *)&s_vendor_panel,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &pcfg, &s_panel), TAG, "panel");
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    /* Orientación del módulo (ver LCD_INTERCAMBIAR_XY y LCD_ESPEJO_* en config.h) */
    esp_lcd_panel_swap_xy(s_panel, LCD_INTERCAMBIAR_XY);
    esp_lcd_panel_mirror(s_panel, LCD_ESPEJO_X, LCD_ESPEJO_Y);
    esp_lcd_panel_disp_on_off(s_panel, true);

    /* Táctil XPT2046 como segundo dispositivo del mismo bus */
    spi_device_interface_config_t tcfg = {
        .clock_speed_hz = TACTIL_SPI_HZ,
        .mode = 0,
        .spics_io_num = PIN_T_CS,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI_BUS, &tcfg, &s_tactil), TAG, "táctil");

    /* Dos buffers de dibujo en RAM interna accesible por DMA: LVGL dibuja en
     * uno mientras el otro se envía a la pantalla */
    size_t bytes = BUFFER_PX * sizeof(uint16_t);
    void *b1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *b2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(b1 && b2, ESP_ERR_NO_MEM, TAG, "buffers");
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, volcar);

    /* Dispositivo de entrada: el táctil como puntero */
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, leer_tactil);

    BaseType_t ok = xTaskCreatePinnedToCore(tarea_lvgl, "lvgl", 8192, NULL, 2, NULL, 0);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "tarea lvgl");
    return ESP_OK;
}
