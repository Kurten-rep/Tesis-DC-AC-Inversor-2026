/*
 * ui.c
 * Una pantalla de LVGL por cada vista del lienzo. Todas comparten la barra
 * de estado superior (estado, canal o aviso, indicador del paro). Los
 * botones no tocan el hardware: solo envían eventos a la máquina de estados.
 */
#include "ui.h"
#include "config.h"
#include "spwm.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "lvgl.h"

LV_FONT_DECLARE(fuente_10);
LV_FONT_DECLARE(fuente_12);
LV_FONT_DECLARE(fuente_14);
LV_FONT_DECLARE(fuente_16);
LV_FONT_DECLARE(fuente_28);

#define F10 (&fuente_10)
#define F12 (&fuente_12)
#define F14 (&fuente_14)
#define F16 (&fuente_16)
#define F28 (&fuente_28)

#define SIMB_OK    "\xEF\x80\x8C"   /* U+F00C */
#define SIMB_X     "\xEF\x80\x8D"   /* U+F00D */

/* Paleta del lienzo */
#define C_FONDO        0x101418
#define C_PANEL        0x1C232B
#define C_LINEA        0x2A333D
#define C_BORDE_BTN    0x3A4552
#define C_TEXTO        0xE8EDF2
#define C_TEXTO_2      0x8A97A6
#define C_TEXTO_3      0xC3CCD6
#define C_AMBAR        0xF2A33A
#define C_CIAN         0x3FB8E0
#define C_VERDE        0x3CCB7F
#define C_ROJO         0xC62F35
#define C_ROJO_TXT     0xFF8A8D
#define C_FALLA_FONDO  0x1A0D0E
#define C_FALLA_LINEA  0x3A2224
#define C_FALLA_TXT_2  0xC9A5A7

typedef enum {
    P_ARRANQUE, P_ERROR, P_ESPERA, P_CFG1, P_CFG2, P_ARMADO, P_OP1, P_OP2, P_FALLA, P_NUM
} pantalla_t;

typedef struct {
    lv_obj_t *barra;
    lv_obj_t *pastilla;
    lv_obj_t *pastilla_txt;
    lv_obj_t *centro;
    lv_obj_t *punto;
    lv_obj_t *paro;
} barra_t;

static ui_cb_evento_t s_cb;
static lv_obj_t *s_scr[P_NUM];
static barra_t s_barra[P_NUM];
static pantalla_t s_actual = P_NUM;

/* Elementos que cambian */
static struct {
    lv_obj_t *barra_cal;
    lv_obj_t *txt_cal;
} s_arr;
static struct {
    lv_obj_t *fila[SENSOR_NUM];
    lv_obj_t *valor[SENSOR_NUM];
    lv_obj_t *marca[SENSOR_NUM];
} s_err;
static struct {
    lv_obj_t *pie;
} s_esp;
static struct {
    lv_obj_t *aviso;
} s_cfg1;
static struct {
    lv_obj_t *fref, *rpm, *ma, *b10k, *b20k, *aviso;
} s_cfg2;
static struct {
    lv_obj_t *titulo, *txt_inactivo, *ico_paro, *ico_tiempo, *progreso, *restante, *derecha;
} s_arm;
static struct {
    lv_obj_t *irms, *f1, *fsw, *ma, *rampa, *rampa_txt;
} s_op1;
static struct {
    lv_obj_t *subt, *f1, *fsw, *ma, *rpm, *rampa, *iu, *iv, *iw;
} s_op2;
static struct {
    lv_obj_t *causa, *pico, *umbral, *canal, *aviso, *boton, *boton_txt;
} s_falla;

/* ------------------------------------------------------------ Utilidades */

static void texto(lv_obj_t *lbl, const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (strcmp(lv_label_get_text(lbl), buf) != 0) {
        lv_label_set_text(lbl, buf);
    }
}

/* Número con coma decimal, como en el documento */
static const char *num(char *buf, size_t n, float v, int dec)
{
    snprintf(buf, n, "%.*f", dec, (double)v);
    for (char *p = buf; *p; p++) {
        if (*p == '.') {
            *p = ',';
        }
    }
    return buf;
}

static lv_obj_t *caja(lv_obj_t *padre)
{
    lv_obj_t *o = lv_obj_create(padre);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *fila_flex(lv_obj_t *padre, lv_flex_flow_t flujo, int gap)
{
    lv_obj_t *o = caja(padre);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, flujo);
    lv_obj_set_style_pad_column(o, gap, 0);
    lv_obj_set_style_pad_row(o, gap, 0);
    return o;
}

static lv_obj_t *etiqueta(lv_obj_t *padre, const lv_font_t *f, uint32_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(padre);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, txt);
    return l;
}

static void al_tocar(lv_event_t *e)
{
    const evento_t *ev = (const evento_t *)lv_event_get_user_data(e);
    if (s_cb && ev) {
        s_cb(ev);
    }
}

static lv_obj_t *boton(lv_obj_t *padre, int w, int h, uint32_t fondo, uint32_t borde,
                       uint32_t color_txt, const lv_font_t *f, const char *txt,
                       const evento_t *ev, bool repetir)
{
    lv_obj_t *b = lv_button_create(padre);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(fondo), 0);
    lv_obj_set_style_bg_color(b, lv_color_darken(lv_color_hex(fondo), LV_OPA_20), LV_STATE_PRESSED);
    if (borde) {
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(borde), 0);
    }
    lv_obj_set_style_bg_color(b, lv_color_hex(C_LINEA), LV_STATE_DISABLED);
    lv_obj_t *l = etiqueta(b, f, color_txt, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(C_TEXTO_2), LV_STATE_DISABLED);
    lv_obj_center(l);
    if (ev) {
        lv_obj_add_event_cb(b, al_tocar, repetir ? LV_EVENT_SHORT_CLICKED : LV_EVENT_CLICKED, (void *)ev);
        if (repetir) {
            lv_obj_add_event_cb(b, al_tocar, LV_EVENT_LONG_PRESSED_REPEAT, (void *)ev);
        }
    }
    return b;
}

static lv_obj_t *barra_progreso(lv_obj_t *padre, int h, uint32_t color)
{
    lv_obj_t *b = lv_bar_create(padre);
    lv_obj_set_size(b, LV_PCT(100), h);
    lv_bar_set_range(b, 0, 100);
    lv_obj_set_style_radius(b, h / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(b, h / 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_LINEA), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    return b;
}

/* Fila "nombre ........ valor" con línea inferior opcional */
static lv_obj_t *fila_dato(lv_obj_t *padre, const char *nombre, const char *valor,
                           bool linea, uint32_t color_nombre, uint32_t color_linea)
{
    lv_obj_t *f = caja(padre);
    lv_obj_set_size(f, LV_PCT(100), 21);
    lv_obj_set_flex_flow(f, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(f, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (linea) {
        lv_obj_set_style_border_side(f, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(f, 1, 0);
        lv_obj_set_style_border_color(f, lv_color_hex(color_linea), 0);
    }
    etiqueta(f, F12, color_nombre, nombre);
    return etiqueta(f, F12, C_TEXTO, valor);
}

static lv_obj_t *crear_pantalla(pantalla_t p, uint32_t fondo)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, lv_color_hex(fondo), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s, 0, 0);
    lv_obj_set_style_pad_row(s, 0, 0);

    barra_t *b = &s_barra[p];
    b->barra = caja(s);
    lv_obj_set_size(b->barra, LV_PCT(100), 28);
    lv_obj_set_style_bg_opa(b->barra, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b->barra, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_border_side(b->barra, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(b->barra, 1, 0);
    lv_obj_set_style_border_color(b->barra, lv_color_hex(C_LINEA), 0);
    lv_obj_set_style_pad_hor(b->barra, 8, 0);
    lv_obj_set_flex_flow(b->barra, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b->barra, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    b->pastilla = caja(b->barra);
    lv_obj_set_size(b->pastilla, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(b->pastilla, 4, 0);
    lv_obj_set_style_bg_opa(b->pastilla, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(b->pastilla, 6, 0);
    lv_obj_set_style_pad_ver(b->pastilla, 2, 0);
    b->pastilla_txt = etiqueta(b->pastilla, F10, C_TEXTO, "");

    b->centro = etiqueta(b->barra, F10, C_TEXTO_2, "");

    lv_obj_t *paro = caja(b->barra);
    lv_obj_set_size(paro, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(paro, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(paro, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(paro, 4, 0);
    b->punto = caja(paro);
    lv_obj_set_size(b->punto, 7, 7);
    lv_obj_set_style_radius(b->punto, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b->punto, LV_OPA_COVER, 0);
    b->paro = etiqueta(paro, F10, C_VERDE, "PARO OK");

    s_scr[p] = s;
    return s;
}

/* Cuerpo flexible debajo de la barra */
static lv_obj_t *cuerpo(lv_obj_t *scr, int pad_h, int pad_v, int gap)
{
    lv_obj_t *c = caja(scr);
    lv_obj_set_width(c, LV_PCT(100));
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(c, pad_h, 0);
    lv_obj_set_style_pad_ver(c, pad_v, 0);
    lv_obj_set_style_pad_row(c, gap, 0);
    return c;
}

static lv_obj_t *relleno(lv_obj_t *padre)
{
    lv_obj_t *r = caja(padre);
    lv_obj_set_width(r, 1);
    lv_obj_set_flex_grow(r, 1);
    return r;
}

/* ------------------------------------------------------- Eventos fijos */

static const evento_t EV_CH1 = { .tipo = EV_SELECCIONAR_CANAL, .valor = CANAL_1 };
static const evento_t EV_CH2 = { .tipo = EV_SELECCIONAR_CANAL, .valor = CANAL_2 };
static const evento_t EV_VOLV = { .tipo = EV_VOLVER };
static const evento_t EV_INIC = { .tipo = EV_INICIAR };
static const evento_t EV_DET = { .tipo = EV_DETENER };
static const evento_t EV_REC = { .tipo = EV_RECONOCER };
static const evento_t EV_REI = { .tipo = EV_REINTENTAR };
static const evento_t EV_M100 = { .tipo = EV_AJUSTAR_FREF, .valor = -CH2_PASO_GRUESO_HZ };
static const evento_t EV_M10 = { .tipo = EV_AJUSTAR_FREF, .valor = -CH2_PASO_FINO_HZ };
static const evento_t EV_P10 = { .tipo = EV_AJUSTAR_FREF, .valor = CH2_PASO_FINO_HZ };
static const evento_t EV_P100 = { .tipo = EV_AJUSTAR_FREF, .valor = CH2_PASO_GRUESO_HZ };
static const evento_t EV_F10K = { .tipo = EV_SELECCIONAR_FSW, .valor = 10000 };
static const evento_t EV_F20K = { .tipo = EV_SELECCIONAR_FSW, .valor = 20000 };

/* ------------------------------------------------------------ Pantallas */

static void crear_arranque(void)
{
    lv_obj_t *s = crear_pantalla(P_ARRANQUE, C_FONDO);
    lv_obj_t *c = cuerpo(s, 24, 0, 8);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    etiqueta(c, F16, C_TEXTO, "Inversor DC-AC didáctico");
    etiqueta(c, F12, C_TEXTO_2, "Universidad El Bosque");
    s_arr.barra_cal = barra_progreso(c, 8, C_CIAN);
    lv_obj_set_width(s_arr.barra_cal, 220);
    s_arr.txt_cal = etiqueta(c, F12, C_TEXTO, "Calibrando sensores a 0 A…");
    etiqueta(c, F10, C_TEXTO_2, "I_CH1, I_U, I_V · rango válido 1,57 V a 1,77 V");
}

static void crear_error(void)
{
    static const char *const nombres[SENSOR_NUM] = { "I_CH1 · GPIO 1", "I_U · GPIO 2", "I_V · GPIO 4" };
    lv_obj_t *s = crear_pantalla(P_ERROR, C_FONDO);
    lv_obj_t *c = cuerpo(s, 12, 10, 5);
    etiqueta(c, F14, C_ROJO_TXT, "Offset de sensor fuera de rango");
    lv_obj_t *d = etiqueta(c, F10, C_TEXTO_2, "Voltaje medido a 0 A con los drivers apagados. Rango válido: 1,57 V a 1,77 V.");
    lv_obj_set_width(d, LV_PCT(100));
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    for (int k = 0; k < SENSOR_NUM; k++) {
        lv_obj_t *f = caja(c);
        lv_obj_set_size(f, LV_PCT(100), 26);
        lv_obj_set_style_radius(f, 6, 0);
        lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(f, lv_color_hex(C_PANEL), 0);
        lv_obj_set_style_pad_hor(f, 8, 0);
        lv_obj_set_flex_flow(f, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(f, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        etiqueta(f, F12, C_TEXTO, nombres[k]);
        s_err.valor[k] = etiqueta(f, F12, C_TEXTO, "-");
        s_err.marca[k] = etiqueta(f, F12, C_VERDE, "OK");
        s_err.fila[k] = f;
    }
    relleno(c);
    lv_obj_t *pie = fila_flex(c, LV_FLEX_FLOW_ROW, 8);
    lv_obj_set_flex_align(pie, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *t = etiqueta(pie, F10, C_TEXTO_2, "Revise la alimentación de 5 V y el cable del sensor.");
    lv_obj_set_flex_grow(t, 1);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    boton(pie, 120, 44, C_CIAN, 0, 0x06212C, F14, "Reintentar", &EV_REI, false);
}

static lv_obj_t *tarjeta_canal(lv_obj_t *padre, uint32_t color, uint32_t fondo, const char *canal,
                               const char *nombre, const char *l1, const char *l2, const char *pie,
                               const evento_t *ev)
{
    lv_obj_t *b = lv_button_create(padre);
    lv_obj_remove_style_all(b);
    lv_obj_set_height(b, LV_PCT(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(fondo), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_PANEL), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_pad_all(b, 10, 0);
    lv_obj_set_style_pad_row(b, 4, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    etiqueta(b, F10, color, canal);
    etiqueta(b, F16, C_TEXTO, nombre);
    etiqueta(b, F12, C_TEXTO_3, l1);
    etiqueta(b, F12, C_TEXTO_3, l2);
    relleno(b);
    etiqueta(b, F10, C_TEXTO_2, pie);
    lv_obj_add_event_cb(b, al_tocar, LV_EVENT_CLICKED, (void *)ev);
    return b;
}

static void crear_espera(void)
{
    lv_obj_t *s = crear_pantalla(P_ESPERA, C_FONDO);
    lv_obj_t *c = cuerpo(s, 10, 10, 6);
    lv_obj_t *fila = caja(c);
    lv_obj_set_width(fila, LV_PCT(100));
    lv_obj_set_flex_grow(fila, 1);
    lv_obj_set_flex_flow(fila, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(fila, 8, 0);
    tarjeta_canal(fila, C_AMBAR, 0x221A10, "CANAL 1", "Monofásico", "120 V · 60 Hz",
                  "SPWM unipolar", "Bus 200 VDC · 40 W", &EV_CH1);
    tarjeta_canal(fila, C_CIAN, 0x0F1E25, "CANAL 2", "Trifásico", "V/f · 10 a 1200 Hz",
                  "Motor A2212/13T", "Bus 12 a 24 VDC", &EV_CH2);
    s_esp.pie = etiqueta(c, F10, C_TEXTO_2, "Toque un canal para configurarlo");
    lv_obj_set_width(s_esp.pie, LV_PCT(100));
    lv_obj_set_style_text_align(s_esp.pie, LV_TEXT_ALIGN_CENTER, 0);
}

static lv_obj_t *fila_botones_inicio(lv_obj_t *padre)
{
    lv_obj_t *f = fila_flex(padre, LV_FLEX_FLOW_ROW, 8);
    boton(f, 96, 44, C_PANEL, C_BORDE_BTN, C_TEXTO, F14, "Volver", &EV_VOLV, false);
    lv_obj_t *b = boton(f, 100, 44, C_VERDE, 0, 0x0B1A12, F16, "INICIAR", &EV_INIC, false);
    lv_obj_set_flex_grow(b, 1);
    return f;
}

static void crear_cfg1(void)
{
    lv_obj_t *s = crear_pantalla(P_CFG1, C_FONDO);
    lv_obj_t *c = cuerpo(s, 12, 8, 3);
    fila_dato(c, "Salida", "120 VRMS · 60 Hz", true, C_TEXTO_2, C_LINEA);
    fila_dato(c, "Índice de modulación", "0,88", true, C_TEXTO_2, C_LINEA);
    fila_dato(c, "Arranque suave", "0 a 0,88 en 1 s", true, C_TEXTO_2, C_LINEA);
    fila_dato(c, "Conmutación · tiempo muerto", "20 kHz · 800 ns", true, C_TEXTO_2, C_LINEA);
    fila_dato(c, "Umbral de sobrecorriente", "±2 A", false, C_TEXTO_2, C_LINEA);
    relleno(c);
    s_cfg1.aviso = etiqueta(c, F10, C_AMBAR, "");
    lv_obj_set_width(s_cfg1.aviso, LV_PCT(100));
    lv_obj_set_style_text_align(s_cfg1.aviso, LV_TEXT_ALIGN_CENTER, 0);
    fila_botones_inicio(c);
}

static void crear_cfg2(void)
{
    lv_obj_t *s = crear_pantalla(P_CFG2, C_FONDO);
    lv_obj_t *c = cuerpo(s, 12, 6, 6);

    lv_obj_t *cab = fila_flex(c, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_flex_align(cab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_t *izq = caja(cab);
    lv_obj_set_size(izq, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(izq, LV_FLEX_FLOW_COLUMN);
    etiqueta(izq, F10, C_TEXTO_2, "Frecuencia de referencia");
    s_cfg2.fref = etiqueta(izq, F28, C_TEXTO, "");
    lv_obj_t *der = caja(cab);
    lv_obj_set_size(der, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(der, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(der, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    s_cfg2.rpm = etiqueta(der, F10, C_TEXTO_2, "");
    s_cfg2.ma = etiqueta(der, F10, C_TEXTO_2, "");

    lv_obj_t *aj = fila_flex(c, LV_FLEX_FLOW_ROW, 6);
    const char *et[4] = { "−100", "−10", "+10", "+100" };
    const evento_t *ev[4] = { &EV_M100, &EV_M10, &EV_P10, &EV_P100 };
    for (int k = 0; k < 4; k++) {
        lv_obj_t *b = boton(aj, 60, 44, C_PANEL, C_BORDE_BTN, C_TEXTO, F14, et[k], ev[k], true);
        lv_obj_set_flex_grow(b, 1);
    }

    lv_obj_t *fs = fila_flex(c, LV_FLEX_FLOW_ROW, 8);
    lv_obj_set_flex_align(fs, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    etiqueta(fs, F10, C_TEXTO_2, "Conmutación base");
    lv_obj_t *seg = caja(fs);
    lv_obj_set_size(seg, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(seg, 4, 0);
    s_cfg2.b10k = boton(seg, 72, 36, C_PANEL, C_BORDE_BTN, C_TEXTO, F12, "10 kHz", &EV_F10K, false);
    s_cfg2.b20k = boton(seg, 72, 36, C_PANEL, C_BORDE_BTN, C_TEXTO, F12, "20 kHz", &EV_F20K, false);
    lv_obj_t *segs[2] = { s_cfg2.b10k, s_cfg2.b20k };
    for (int k = 0; k < 2; k++) {
        lv_obj_set_style_bg_color(segs[k], lv_color_hex(C_CIAN), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(lv_obj_get_child(segs[k], 0), lv_color_hex(0x06212C), LV_STATE_CHECKED);
    }

    relleno(c);
    s_cfg2.aviso = etiqueta(c, F10, C_AMBAR, "");
    lv_obj_set_width(s_cfg2.aviso, LV_PCT(100));
    lv_obj_set_style_text_align(s_cfg2.aviso, LV_TEXT_ALIGN_CENTER, 0);
    fila_botones_inicio(c);
}

static lv_obj_t *fila_chequeo(lv_obj_t *padre, const char *txt, lv_obj_t **ico, lv_obj_t **lbl)
{
    lv_obj_t *f = fila_flex(padre, LV_FLEX_FLOW_ROW, 8);
    lv_obj_set_flex_align(f, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    *ico = etiqueta(f, F14, C_VERDE, SIMB_OK);
    lv_obj_t *l = etiqueta(f, F12, C_TEXTO, txt);
    if (lbl) {
        *lbl = l;
    }
    return f;
}

static void crear_armado(void)
{
    lv_obj_t *s = crear_pantalla(P_ARMADO, C_FONDO);
    lv_obj_t *c = cuerpo(s, 16, 14, 8);
    s_arm.titulo = etiqueta(c, F16, C_TEXTO, "");
    lv_obj_t *ico_inactivo;
    fila_chequeo(c, "", &ico_inactivo, &s_arm.txt_inactivo);
    fila_chequeo(c, "Paro de emergencia liberado", &s_arm.ico_paro, NULL);
    fila_chequeo(c, "Compuertas apagadas 500 ms", &s_arm.ico_tiempo, NULL);
    relleno(c);
    s_arm.progreso = barra_progreso(c, 10, C_AMBAR);
    lv_obj_t *pie = fila_flex(c, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_flex_align(pie, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_arm.restante = etiqueta(pie, F10, C_TEXTO_2, "");
    s_arm.derecha = etiqueta(pie, F10, C_TEXTO_2, "");
}

static void crear_op1(void)
{
    lv_obj_t *s = crear_pantalla(P_OP1, C_FONDO);
    lv_obj_t *c = cuerpo(s, 12, 8, 6);

    lv_obj_t *arriba = fila_flex(c, LV_FLEX_FLOW_ROW, 8);
    lv_obj_t *tarj = caja(arriba);
    lv_obj_set_height(tarj, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(tarj, 1);
    lv_obj_set_style_bg_opa(tarj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tarj, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_radius(tarj, 8, 0);
    lv_obj_set_style_pad_all(tarj, 8, 0);
    lv_obj_set_flex_flow(tarj, LV_FLEX_FLOW_COLUMN);
    etiqueta(tarj, F10, C_TEXTO_2, "Corriente de salida");
    s_op1.irms = etiqueta(tarj, F28, C_AMBAR, "");
    etiqueta(tarj, F10, C_TEXTO_2, "RMS · umbral ±2 A");

    lv_obj_t *datos = caja(arriba);
    lv_obj_set_height(datos, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(datos, 1);
    lv_obj_set_flex_flow(datos, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(datos, 3, 0);
    s_op1.f1 = fila_dato(datos, "f1", "", true, C_TEXTO_2, C_LINEA);
    s_op1.fsw = fila_dato(datos, "fsw", "", true, C_TEXTO_2, C_LINEA);
    s_op1.ma = fila_dato(datos, "ma", "", false, C_TEXTO_2, C_LINEA);

    lv_obj_t *r = fila_flex(c, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    etiqueta(r, F10, C_TEXTO_2, "Rampa de ma");
    s_op1.rampa_txt = etiqueta(r, F10, C_TEXTO_2, "");
    s_op1.rampa = barra_progreso(c, 6, C_AMBAR);

    relleno(c);
    lv_obj_t *b = boton(c, 100, 48, C_ROJO, 0, 0xFFFFFF, F16, "DETENER", &EV_DET, false);
    lv_obj_set_width(b, LV_PCT(100));
}

static lv_obj_t *caja_corriente(lv_obj_t *padre, const char *nombre)
{
    lv_obj_t *t = caja(padre);
    lv_obj_set_height(t, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(t, 1);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(t, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_radius(t, 6, 0);
    lv_obj_set_style_pad_hor(t, 6, 0);
    lv_obj_set_style_pad_ver(t, 4, 0);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    etiqueta(t, F10, C_TEXTO_2, nombre);
    return etiqueta(t, F14, C_TEXTO, "");
}

static void crear_op2(void)
{
    lv_obj_t *s = crear_pantalla(P_OP2, C_FONDO);
    lv_obj_t *c = cuerpo(s, 10, 6, 5);

    lv_obj_t *cab = fila_flex(c, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_flex_align(cab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_t *izq = caja(cab);
    lv_obj_set_size(izq, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(izq, LV_FLEX_FLOW_COLUMN);
    s_op2.subt = etiqueta(izq, F10, C_TEXTO_2, "");
    s_op2.f1 = etiqueta(izq, F28, C_CIAN, "");
    lv_obj_t *der = caja(cab);
    lv_obj_set_size(der, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(der, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(der, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    s_op2.fsw = etiqueta(der, F10, C_TEXTO, "");
    s_op2.ma = etiqueta(der, F10, C_TEXTO, "");
    s_op2.rpm = etiqueta(der, F10, C_TEXTO, "");

    s_op2.rampa = barra_progreso(c, 6, C_CIAN);

    lv_obj_t *corr = fila_flex(c, LV_FLEX_FLOW_ROW, 6);
    s_op2.iu = caja_corriente(corr, "I_U RMS");
    s_op2.iv = caja_corriente(corr, "I_V RMS");
    s_op2.iw = caja_corriente(corr, "I_W RMS (calc.)");
    etiqueta(c, F10, C_TEXTO_2, "Umbral ±15 A por fase");

    relleno(c);
    lv_obj_t *pie = fila_flex(c, LV_FLEX_FLOW_ROW, 6);
    boton(pie, 60, 44, C_PANEL, C_BORDE_BTN, C_TEXTO, F14, "−10", &EV_M10, true);
    boton(pie, 60, 44, C_PANEL, C_BORDE_BTN, C_TEXTO, F14, "+10", &EV_P10, true);
    lv_obj_t *b = boton(pie, 100, 44, C_ROJO, 0, 0xFFFFFF, F16, "DETENER", &EV_DET, false);
    lv_obj_set_flex_grow(b, 1);
}

static void crear_falla(void)
{
    lv_obj_t *s = crear_pantalla(P_FALLA, C_FALLA_FONDO);
    barra_t *br = &s_barra[P_FALLA];
    lv_obj_set_style_bg_color(br->barra, lv_color_hex(C_ROJO), 0);
    lv_obj_set_style_border_width(br->barra, 0, 0);

    lv_obj_t *c = cuerpo(s, 12, 10, 6);
    s_falla.causa = etiqueta(c, F16, C_ROJO_TXT, "");
    lv_obj_t *tabla = fila_flex(c, LV_FLEX_FLOW_COLUMN, 3);
    s_falla.pico = fila_dato(tabla, "Pico medido", "", true, C_FALLA_TXT_2, C_FALLA_LINEA);
    s_falla.umbral = fila_dato(tabla, "Umbral", "", true, C_FALLA_TXT_2, C_FALLA_LINEA);
    s_falla.canal = fila_dato(tabla, "Canal · f1 al disparo", "", false, C_FALLA_TXT_2, C_FALLA_LINEA);
    s_falla.aviso = etiqueta(c, F10, C_FALLA_TXT_2,
        "Revise la carga y las conexiones antes de reconocer. Si el paro está presionado, el botón queda inactivo.");
    lv_obj_set_width(s_falla.aviso, LV_PCT(100));
    lv_label_set_long_mode(s_falla.aviso, LV_LABEL_LONG_WRAP);
    relleno(c);
    s_falla.boton = boton(c, 100, 44, C_AMBAR, 0, 0x2A1A04, F16, "RECONOCER FALLA", &EV_REC, false);
    lv_obj_set_width(s_falla.boton, LV_PCT(100));
}

/* ----------------------------------------------------------- Público */

void ui_iniciar(ui_cb_evento_t cb)
{
    s_cb = cb;
    crear_arranque();
    crear_error();
    crear_espera();
    crear_cfg1();
    crear_cfg2();
    crear_armado();
    crear_op1();
    crear_op2();
    crear_falla();
    lv_screen_load(s_scr[P_ARRANQUE]);
    s_actual = P_ARRANQUE;
}

static pantalla_t pantalla_de(const sm_t *sm)
{
    switch (sm->estado) {
    case EST_ARRANQUE:      return P_ARRANQUE;
    case EST_ERROR_SENSOR:  return P_ERROR;
    case EST_ESPERA:        return P_ESPERA;
    case EST_CONFIGURACION: return (sm->par.canal == CANAL_1) ? P_CFG1 : P_CFG2;
    case EST_ARMADO:        return P_ARMADO;
    case EST_OPERACION:     return (sm->par.canal == CANAL_1) ? P_OP1 : P_OP2;
    case EST_FALLA:         return P_FALLA;
    default:                return P_ESPERA;
    }
}

static void actualizar_barra(pantalla_t p, const ui_datos_t *d)
{
    const sm_t *sm = d->sm;
    barra_t *b = &s_barra[p];
    uint32_t fondo = C_LINEA, txt = C_TEXTO;
    switch (sm->estado) {
    case EST_ERROR_SENSOR: fondo = 0x5A1D20; txt = 0xFFB3B5; break;
    case EST_ARMADO:       fondo = 0x4A3A12; txt = 0xFFD58A; break;
    case EST_OPERACION:    fondo = 0x14432A; txt = 0x8FF0BA; break;
    case EST_FALLA:        fondo = C_ROJO;   txt = 0xFFFFFF; break;
    default: break;
    }
    lv_obj_set_style_bg_color(b->pastilla, lv_color_hex(fondo), 0);
    lv_obj_set_style_text_color(b->pastilla_txt, lv_color_hex(txt), 0);
    texto(b->pastilla_txt, "%s", sm_nombre_estado(sm->estado));

    uint32_t c_centro = C_TEXTO_2;
    if (sm->estado == EST_FALLA) {
        texto(b->centro, "Drivers apagados");
        c_centro = 0xFFFFFF;
    } else if (sm->par.canal == CANAL_1) {
        texto(b->centro, "Canal 1 · Monofásico");
        c_centro = C_AMBAR;
    } else if (sm->par.canal == CANAL_2) {
        texto(b->centro, "Canal 2 · Trifásico");
        c_centro = C_CIAN;
    } else if (sm->estado == EST_ERROR_SENSOR) {
        texto(b->centro, "Operación bloqueada");
    } else {
        texto(b->centro, "Drivers apagados");
    }
    lv_obj_set_style_text_color(b->centro, lv_color_hex(c_centro), 0);

    uint32_t c_paro = d->paro ? C_ROJO_TXT : C_VERDE;
    if (sm->estado == EST_FALLA) {
        c_paro = d->paro ? 0xFFFFFF : 0x8FF0BA;
        texto(b->paro, d->paro ? "PARO ACTIVO" : "PARO LIBERADO");
    } else {
        texto(b->paro, d->paro ? "PARO ACTIVO" : "PARO OK");
    }
    lv_obj_set_style_text_color(b->paro, lv_color_hex(c_paro), 0);
    lv_obj_set_style_bg_color(b->punto, lv_color_hex(c_paro), 0);
}

static void aviso_o(lv_obj_t *lbl, const sm_t *sm, const char *defecto)
{
    texto(lbl, "%s", sm->aviso ? sm->aviso : defecto);
}

void ui_actualizar(const ui_datos_t *d)
{
    const sm_t *sm = d->sm;
    char a[16], b[16];
    pantalla_t p = pantalla_de(sm);
    if (p != s_actual) {
        lv_screen_load(s_scr[p]);
        s_actual = p;
    }
    actualizar_barra(p, d);

    switch (p) {
    case P_ARRANQUE:
        lv_bar_set_value(s_arr.barra_cal, (int32_t)((d->ahora_ms / 4) % 100), LV_ANIM_OFF);
        break;

    case P_ERROR:
        for (int k = 0; k < SENSOR_NUM; k++) {
            bool ok = d->med.offset_ok[k];
            texto(s_err.valor[k], "%s V", num(a, sizeof a, d->med.offset_mv[k] / 1000.0f, 3));
            texto(s_err.marca[k], ok ? "OK" : "FUERA");
            lv_obj_set_style_text_color(s_err.marca[k], lv_color_hex(ok ? C_VERDE : C_ROJO_TXT), 0);
            lv_obj_set_style_bg_color(s_err.fila[k], lv_color_hex(ok ? C_PANEL : 0x2B1416), 0);
            lv_obj_set_style_border_width(s_err.fila[k], ok ? 0 : 1, 0);
            lv_obj_set_style_border_color(s_err.fila[k], lv_color_hex(0xE5484D), 0);
        }
        break;

    case P_ESPERA:
        aviso_o(s_esp.pie, sm, d->paro ? "Paro presionado: libérelo para operar" : "Toque un canal para configurarlo");
        break;

    case P_CFG1:
        aviso_o(s_cfg1.aviso, sm, "Conecte el bus de 200 V y desconecte el bus del Canal 2");
        break;

    case P_CFG2: {
        float f = (float)sm->par.f_ref_hz;
        texto(s_cfg2.fref, "%u Hz", (unsigned)sm->par.f_ref_hz);
        texto(s_cfg2.rpm, "≈ %u rpm", (unsigned)lroundf(f * 120.0f / CH2_POLOS));
        texto(s_cfg2.ma, "ma ≈ %s", num(a, sizeof a, vf_ma(f, CH2_MA0, CH2_F1_MAX_HZ), 2));
        if (sm->par.fsw_base_hz == 10000) {
            lv_obj_add_state(s_cfg2.b10k, LV_STATE_CHECKED);
            lv_obj_remove_state(s_cfg2.b20k, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(s_cfg2.b20k, LV_STATE_CHECKED);
            lv_obj_remove_state(s_cfg2.b10k, LV_STATE_CHECKED);
        }
        aviso_o(s_cfg2.aviso, sm, "Conecte el bus de 12 V a 24 V y desconecte el del Canal 1");
        break;
    }

    case P_ARMADO: {
        uint32_t t = d->ahora_ms - sm->t_entrada_ms;
        if (t > ARMADO_ESPERA_MS) t = ARMADO_ESPERA_MS;
        bool ch1 = (sm->par.canal == CANAL_1);
        texto(s_arm.titulo, "Armando Canal %d…", ch1 ? 1 : 2);
        texto(s_arm.txt_inactivo, "Canal %d con SD en alto (deshabilitado)", ch1 ? 2 : 1);
        texto(s_arm.ico_paro, "%s", d->paro ? SIMB_X : SIMB_OK);
        lv_obj_set_style_text_color(s_arm.ico_paro, lv_color_hex(d->paro ? C_ROJO_TXT : C_VERDE), 0);
        bool listo = (t >= ARMADO_ESPERA_MS);
        lv_obj_set_style_text_color(s_arm.ico_tiempo, lv_color_hex(listo ? C_VERDE : C_AMBAR), 0);
        lv_bar_set_value(s_arm.progreso, (int32_t)(t * 100 / ARMADO_ESPERA_MS), LV_ANIM_OFF);
        texto(s_arm.restante, "Habilitando en %s s", num(a, sizeof a, (ARMADO_ESPERA_MS - t) / 1000.0f, 1));
        texto(s_arm.derecha, ch1 ? "ma inicial 0" : "f1 inicial 10 Hz");
        break;
    }

    case P_OP1: {
        texto(s_op1.irms, "%s A", num(a, sizeof a, d->med.rms_a[SENSOR_CH1], 2));
        texto(s_op1.f1, "%u Hz", (unsigned)lroundf(d->pot.f1_hz));
        texto(s_op1.fsw, "%s kHz", num(a, sizeof a, d->pot.fsw_hz / 1000.0f, 1));
        texto(s_op1.ma, "%s", num(b, sizeof b, d->pot.ma, 2));
        lv_bar_set_value(s_op1.rampa, (int32_t)lroundf(100.0f * d->pot.ma / CH1_MA_FINAL), LV_ANIM_OFF);
        texto(s_op1.rampa_txt, d->pot.rampa_completa ? "completa" : "subiendo");
        break;
    }

    case P_OP2: {
        texto(s_op2.subt, "f1 actual · ref %u Hz", (unsigned)sm->par.f_ref_hz);
        texto(s_op2.f1, "%u Hz", (unsigned)lroundf(d->pot.f1_hz));
        texto(s_op2.fsw, "fsw %s kHz", num(a, sizeof a, d->pot.fsw_hz / 1000.0f, 1));
        texto(s_op2.ma, "ma %s", num(b, sizeof b, d->pot.ma, 2));
        texto(s_op2.rpm, "≈ %u rpm", (unsigned)lroundf(d->pot.f1_hz * 120.0f / CH2_POLOS));
        float ref = d->pot.f_ref_hz > 0 ? d->pot.f_ref_hz : 1.0f;
        int32_t pr = (int32_t)lroundf(100.0f * d->pot.f1_hz / ref);
        lv_bar_set_value(s_op2.rampa, pr > 100 ? 100 : pr, LV_ANIM_OFF);
        texto(s_op2.iu, "%s A", num(a, sizeof a, d->med.rms_a[1], 2));
        texto(s_op2.iv, "%s A", num(a, sizeof a, d->med.rms_a[2], 2));
        texto(s_op2.iw, "%s A", num(a, sizeof a, d->med.rms_a[3], 2));
        break;
    }

    case P_FALLA: {
        texto(s_falla.causa, "%s", sm_texto_causa(sm->causa));
        bool por_corriente = (sm->causa >= CAUSA_SOBRECORRIENTE_CH1 && sm->causa <= CAUSA_SOBRECORRIENTE_W);
        if (por_corriente) {
            texto(s_falla.pico, "%s A", num(a, sizeof a, fabsf(sm->pico_a), 1));
            texto(s_falla.umbral, sm->causa == CAUSA_SOBRECORRIENTE_CH1 ? "±2 A" : "±15 A");
        } else {
            texto(s_falla.pico, "-");
            texto(s_falla.umbral, "-");
        }
        if (sm->canal_falla == CANAL_NINGUNO) {
            texto(s_falla.canal, "-");
        } else {
            texto(s_falla.canal, "Canal %d · %d Hz", (int)sm->canal_falla, (int)sm->f1_falla_hz);
        }
        aviso_o(s_falla.aviso, sm,
            "Revise la carga y las conexiones antes de reconocer. Si el paro está presionado, el botón queda inactivo.");
        if (d->paro) {
            lv_obj_add_state(s_falla.boton, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(s_falla.boton, LV_STATE_DISABLED);
        }
        break;
    }

    default:
        break;
    }
}
