/*
 * pantalla.c
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

#define SPI_BUS SPI2_HOST
#define BUFFER_PX (LCD_H_RES * LCD_LINEAS_BUFFER)

static esp_lcd_panel_handle_t s_panel;
static spi_device_handle_t s_tactil;
static lv_display_t *s_disp;
static pantalla_cb_listo_t s_cb_listo;

/* --------------------------------------------------------------- LVGL */

static uint32_t reloj_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool IRAM_ATTR fin_envio(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ed, void *ctx)
{
    lv_display_flush_ready((lv_display_t *)ctx);
    return false;
}

static void volcar(lv_display_t *disp, const lv_area_t *a, uint8_t *px)
{
    /* El ILI9341 espera RGB565 con el byte alto primero */
    lv_draw_sw_rgb565_swap(px, lv_area_get_size(a));
    esp_lcd_panel_draw_bitmap(s_panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
}

/* ------------------------------------------------------------- XPT2046 */

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

static int mapear(int v, int vmin, int vmax, int salida_max, bool invertir)
{
    int r = (v - vmin) * salida_max / (vmax - vmin);
    if (r < 0) r = 0;
    if (r > salida_max - 1) r = salida_max - 1;
    return invertir ? (salida_max - 1 - r) : r;
}

static void leer_tactil(lv_indev_t *indev, lv_indev_data_t *data)
{
    static int32_t ult_x, ult_y;
    /* Presión: Z1 = 0xB1, Z2 = 0xC1. Con PD = 01 el ADC queda encendido
     * entre lecturas y el IRQ desactivado, lo que da lecturas estables. */
    int z1 = (int)xpt_leer(0xB1);
    int z2 = (int)xpt_leer(0xC1);
    int z = z1 + 4095 - z2;
    if (z1 < 30 || z < TACTIL_Z_UMBRAL) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = ult_x;
        data->point.y = ult_y;
        return;
    }
    /* Promedio de 4 lecturas; la primera de cada eje se descarta */
    int sx = 0, sy = 0;
    xpt_leer(0x91);
    for (int k = 0; k < 4; k++) {
        sy += xpt_leer(0x91);
        sx += xpt_leer(0xD1);
    }
    xpt_leer(0xD0);   /* deja el XPT2046 en reposo con IRQ activo */
    int rx = sx / 4, ry = sy / 4;
#if TACTIL_DEPURAR
    ESP_LOGI(TAG, "tactil crudo x=%d y=%d z=%d", rx, ry, z);
#endif
#if TACTIL_INTERCAMBIAR_XY
    int tmp = rx; rx = ry; ry = tmp;
#endif
    ult_x = mapear(rx, TACTIL_X_MIN, TACTIL_X_MAX, LCD_H_RES, TACTIL_INVERTIR_X);
    ult_y = mapear(ry, TACTIL_Y_MIN, TACTIL_Y_MAX, LCD_V_RES, TACTIL_INVERTIR_Y);
    data->point.x = ult_x;
    data->point.y = ult_y;
    data->state = LV_INDEV_STATE_PRESSED;
}

/* --------------------------------------------------------------- Tarea */

static void tarea_lvgl(void *arg)
{
    if (s_cb_listo) {
        s_cb_listo();
    }
    for (;;) {
        uint32_t espera = lv_timer_handler();
        if (espera > 20) espera = 20;
        if (espera < 2) espera = 2;
        vTaskDelay(pdMS_TO_TICKS(espera));
    }
}

esp_err_t pantalla_iniciar(pantalla_cb_listo_t cb_listo)
{
    s_cb_listo = cb_listo;

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SPI_SCK,
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BUFFER_PX * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI_BUS, &bus, SPI_DMA_CH_AUTO), TAG, "bus SPI");

    lv_init();
    lv_tick_set_cb(reloj_ms);
    s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);

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

    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = -1,   /* TFT_RST va al EN de la placa */
#if LCD_ORDEN_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &pcfg, &s_panel), TAG, "panel");
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_swap_xy(s_panel, LCD_INTERCAMBIAR_XY);
    esp_lcd_panel_mirror(s_panel, LCD_ESPEJO_X, LCD_ESPEJO_Y);
    esp_lcd_panel_disp_on_off(s_panel, true);

    spi_device_interface_config_t tcfg = {
        .clock_speed_hz = TACTIL_SPI_HZ,
        .mode = 0,
        .spics_io_num = PIN_T_CS,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI_BUS, &tcfg, &s_tactil), TAG, "táctil");

    size_t bytes = BUFFER_PX * sizeof(uint16_t);
    void *b1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *b2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(b1 && b2, ESP_ERR_NO_MEM, TAG, "buffers");
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, volcar);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, leer_tactil);

    BaseType_t ok = xTaskCreatePinnedToCore(tarea_lvgl, "lvgl", 8192, NULL, 2, NULL, 0);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "tarea lvgl");
    return ESP_OK;
}
