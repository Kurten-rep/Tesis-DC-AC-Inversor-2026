/*
 * ui.c
 * Una pantalla de LVGL por cada vista del lienzo. Todas comparten la barra
 * de estado superior (estado, canal o aviso, indicador del paro). Los
 * botones no tocan el hardware: solo envían eventos a la máquina de estados.
 *
 * Estructura del archivo:
 *   1. Fuentes, símbolos y paleta de colores.
 *   2. Utilidades para crear objetos de LVGL (cajas, etiquetas, botones...).
 *   3. Eventos fijos que los botones envían.
 *   4. Una función crear_*() por pantalla, que construye sus objetos una
 *      sola vez al arrancar.
 *   5. ui_iniciar() y ui_actualizar(): la segunda se llama cada 50 ms y solo
 *      cambia los textos y colores que dependen de los datos.
 *
 * Las pantallas se crean todas al inicio y se conmutan con lv_screen_load();
 * no se destruyen ni se vuelven a crear.
 */
#include "ui.h"
#include "config.h"
#include "spwm.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "lvgl.h"

/* Fuentes generadas con lv_font_conv (carpeta main/fuentes), Montserrat
 * SemiBold y Bold. El número es el tamaño en píxeles. Incluyen los
 * símbolos de FontAwesome usados abajo. */
LV_FONT_DECLARE(fuente_10);
LV_FONT_DECLARE(fuente_12);
LV_FONT_DECLARE(fuente_14);
LV_FONT_DECLARE(fuente_16);
LV_FONT_DECLARE(fuente_28);
LV_FONT_DECLARE(fuente_negrita_10);
LV_FONT_DECLARE(fuente_negrita_14);
LV_FONT_DECLARE(fuente_negrita_16);
LV_FONT_DECLARE(fuente_negrita_20);

/* Nombres cortos para usar las fuentes en el resto del archivo */
#define F10 (&fuente_10)
#define F12 (&fuente_12)
#define F14 (&fuente_14)
#define F16 (&fuente_16)
#define F28 (&fuente_28)
/* Negrita (Montserrat Bold), para los títulos de canal */
#define FN10 (&fuente_negrita_10)
#define FN14 (&fuente_negrita_14)
#define FN16 (&fuente_negrita_16)
#define FN20 (&fuente_negrita_20)

/* Símbolos de FontAwesome, escritos como secuencia UTF-8 del punto de código */
#define SIMB_OK    "\xEF\x80\x8C"   /* U+F00C */
#define SIMB_X     "\xEF\x80\x8D"   /* U+F00D */
#define SIMB_ALERTA "\xEF\x81\xB1"  /* U+F071 */

/* Paleta del lienzo. Todos los valores se escriben como 0xRRGGBB (24 bits)
 * y se pasan a lv_color_hex(); no usar el formato RGB565 de 16 bits porque
 * se interpretaría como un color distinto.
 *   C_FONDO       fondo de todas las pantallas
 *   C_PANEL       fondo de tarjetas, filas y botones secundarios
 *   C_LINEA       líneas divisorias y barras vacías
 *   C_BORDE_BTN   borde de los botones secundarios
 *   C_TEXTO*      texto principal y secundarios
 *   C_AMBAR       color del Canal 1 y de las advertencias
 *   C_CIAN        color del Canal 2 y de los elementos activos
 *   C_VERDE       confirmaciones (OK)
 *   C_ROJO        fondos de falla y del botón DETENER
 *   C_VERDE_PARO  indicador "PARO OK" de la barra superior
 *   C_ROJO_TXT    texto de errores sobre fondo oscuro
 *   C_FALLA_*     colores propios de la pantalla de falla */
#define C_FONDO        0x000000
#define C_PANEL        0x000c
#define C_LINEA        0x2A333D
#define C_BORDE_BTN    0x3A4552
#define C_TEXTO        0xFFFFFF
#define C_TEXTO_2      0xFFFFFF
#define C_TEXTO_3      0xFFFFFF
#define C_AMBAR        0x0066FF
#define C_CIAN         0xEDED00
#define C_VERDE        0xf800
#define C_ROJO         0x520D0D
#define C_VERDE_PARO   0xf800   /* indicador PARO OK */
#define C_ROJO_TXT     0x520D0D
#define C_FALLA_FONDO  0x280004
#define C_FALLA_LINEA  0x5A0A10
#define C_FALLA_TXT_2  0xFF9A9E

/* Identificador de cada pantalla; P_NUM es la cantidad y sirve de tamaño
 * para los arreglos y de valor "ninguna" al arrancar. */
typedef enum {
    P_ARRANQUE, P_ERROR, P_ESPERA, P_CFG1, P_CFG2, P_ARMADO, P_OP1, P_OP2, P_FALLA, P_NUM
} pantalla_t;

/* Objetos de la barra de estado superior, que existe en cada pantalla */
typedef struct {
    lv_obj_t *barra;          /* contenedor de la barra */
    lv_obj_t *pastilla;       /* cápsula a la izquierda con el nombre del estado */
    lv_obj_t *pastilla_txt;   /* texto del estado dentro de la cápsula */
    lv_obj_t *centro;         /* texto central: canal o "Drivers apagados" */
    lv_obj_t *punto;          /* círculo de color a la izquierda del texto del paro */
    lv_obj_t *paro;           /* texto "PARO OK" / "PARO ACTIVO" */
} barra_t;

static ui_cb_evento_t s_cb;              /* función a la que se envían los eventos de los botones */
static lv_obj_t *s_scr[P_NUM];           /* pantallas de LVGL, una por vista */
static barra_t s_barra[P_NUM];           /* barra superior de cada pantalla */
static pantalla_t s_actual = P_NUM;      /* pantalla visible ahora (P_NUM = ninguna aún) */

/* Elementos que cambian: cada pantalla guarda los punteros a los objetos
 * cuyo texto o color se actualiza en ui_actualizar(). */
static struct {
    lv_obj_t *barra_cal;      /* barra de progreso de la calibración */
    lv_obj_t *txt_cal;
} s_arr;
static struct {
    lv_obj_t *fila[SENSOR_NUM];   /* fila de cada sensor */
    lv_obj_t *valor[SENSOR_NUM];  /* offset medido, en V */
    lv_obj_t *marca[SENSOR_NUM];  /* "OK" o "FUERA" */
} s_err;
static struct {
    lv_obj_t *pie;            /* texto inferior de la pantalla de selección */
} s_esp;
static struct {
    lv_obj_t *aviso;          /* línea de aviso sobre los botones */
} s_cfg1;
static struct {
    lv_obj_t *fref, *b10k, *b20k, *aviso;   /* frecuencia, botones 10 / 20 kHz y aviso */
} s_cfg2;
static struct {
    lv_obj_t *titulo, *txt_inactivo, *ico_paro, *ico_tiempo, *progreso, *restante, *derecha;
} s_arm;
static struct {
    lv_obj_t *irms, *f1, *fsw, *ma, *rampa, *rampa_txt;
} s_op1;
static struct {
    lv_obj_t *subt, *f1, *fsw, *ma, *rampa, *iu, *iv, *iw;
} s_op2;
static struct {
    lv_obj_t *causa, *detalle, *pico, *canal, *aviso, *boton;
} s_falla;

/* ------------------------------------------------------------ Utilidades */

/* Escribe un texto con formato (como printf) en una etiqueta, pero solo si
 * cambió. Así LVGL no redibuja la etiqueta cada 50 ms sin necesidad. */
static void texto(lv_obj_t *lbl, const char *fmt, ...)
{
    char buf[160];   /* los textos de la pantalla de falla pasan de 64 bytes */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (strcmp(lv_label_get_text(lbl), buf) != 0) {
        lv_label_set_text(lbl, buf);
    }
}

/* Número con coma decimal, como en el documento. Escribe en "buf" y lo
 * devuelve para poder usarlo dentro de otro texto. */
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

/* Contenedor vacío, sin estilo, sin desplazamiento y sin respuesta al toque.
 * Es la base de casi todos los bloques de la interfaz. */
static lv_obj_t *caja(lv_obj_t *padre)
{
    lv_obj_t *o = lv_obj_create(padre);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/* Fila (o columna) que ocupa todo el ancho y crece según su contenido,
 * con separación "gap" entre sus elementos. */
static lv_obj_t *fila_flex(lv_obj_t *padre, lv_flex_flow_t flujo, int gap)
{
    lv_obj_t *o = caja(padre);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, flujo);
    lv_obj_set_style_pad_column(o, gap, 0);
    lv_obj_set_style_pad_row(o, gap, 0);
    return o;
}

/* Etiqueta de texto con la fuente y el color (0xRRGGBB) indicados. */
static lv_obj_t *etiqueta(lv_obj_t *padre, const lv_font_t *f, uint32_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(padre);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, txt);
    return l;
}

/* Callback común de todos los botones: reenvía a la máquina de estados el
 * evento que se asoció al botón al crearlo (user data). */
static void al_tocar(lv_event_t *e)
{
    const evento_t *ev = (const evento_t *)lv_event_get_user_data(e);
    if (s_cb && ev) {
        s_cb(ev);
    }
}

/* Botón rectangular con texto centrado.
 *   w, h        tamaño en píxeles
 *   fondo       color de fondo; al presionar se oscurece un 20 %
 *   borde       color del borde, o 0 para no dibujar borde
 *   color_txt   color del texto
 *   ev          evento que se envía al tocar (puede ser NULL)
 *   repetir     true: el evento se repite mientras se mantiene presionado
 *               (para los botones de +/- frecuencia) */
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
    /* Estado deshabilitado: fondo y texto apagados */
    lv_obj_set_style_bg_color(b, lv_color_hex(C_LINEA), LV_STATE_DISABLED);
    lv_obj_t *l = etiqueta(b, f, color_txt, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(C_TEXTO_2), LV_STATE_DISABLED);
    lv_obj_center(l);
    if (ev) {
        /* Con repetición, el toque corto envía una vez y la pulsación larga
         * repite; sin repetición basta el clic */
        lv_obj_add_event_cb(b, al_tocar, repetir ? LV_EVENT_SHORT_CLICKED : LV_EVENT_CLICKED, (void *)ev);
        if (repetir) {
            lv_obj_add_event_cb(b, al_tocar, LV_EVENT_LONG_PRESSED_REPEAT, (void *)ev);
        }
    }
    return b;
}

/* Barra de progreso de 0 a 100, redondeada, de altura h y color del
 * indicador "color". El fondo es C_LINEA. */
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

/* Fila "nombre ........ valor" con línea inferior opcional. Devuelve la
 * etiqueta del valor, para poder actualizarla después. */
static lv_obj_t *fila_dato(lv_obj_t *padre, const char *nombre, const char *valor,
                           bool linea, uint32_t color_nombre, uint32_t color_linea)
{
    lv_obj_t *f = caja(padre);
    lv_obj_set_size(f, LV_PCT(100), 21);
    lv_obj_set_flex_flow(f, LV_FLEX_FLOW_ROW);
    /* SPACE_BETWEEN manda el nombre a la izquierda y el valor a la derecha */
    lv_obj_set_flex_align(f, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (linea) {
        lv_obj_set_style_border_side(f, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(f, 1, 0);
        lv_obj_set_style_border_color(f, lv_color_hex(color_linea), 0);
    }
    etiqueta(f, F12, color_nombre, nombre);
    return etiqueta(f, F12, C_TEXTO, valor);
}

/* Crea una pantalla vacía de fondo "fondo", en columna, con su barra de
 * estado superior ya armada (pastilla de estado, texto central, indicador
 * del paro). Los cuerpos de cada pantalla se agregan debajo. */
static lv_obj_t *crear_pantalla(pantalla_t p, uint32_t fondo)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, lv_color_hex(fondo), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s, 0, 0);
    lv_obj_set_style_pad_row(s, 0, 0);

    /* Barra superior de 28 px de alto con línea inferior */
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

    /* Izquierda: cápsula con el nombre del estado (su color lo fija
     * actualizar_barra()) */
    b->pastilla = caja(b->barra);
    lv_obj_set_size(b->pastilla, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(b->pastilla, 4, 0);
    lv_obj_set_style_bg_opa(b->pastilla, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(b->pastilla, 6, 0);
    lv_obj_set_style_pad_ver(b->pastilla, 2, 0);
    b->pastilla_txt = etiqueta(b->pastilla, F10, C_TEXTO, "");

    /* Centro: canal activo o mensaje */
    b->centro = etiqueta(b->barra, F10, C_TEXTO_2, "");

    /* Derecha: punto de color y texto del paro de emergencia */
    lv_obj_t *paro = caja(b->barra);
    lv_obj_set_size(paro, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(paro, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(paro, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(paro, 4, 0);
    b->punto = caja(paro);
    lv_obj_set_size(b->punto, 7, 7);
    lv_obj_set_style_radius(b->punto, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b->punto, LV_OPA_COVER, 0);
    b->paro = etiqueta(paro, F10, C_VERDE_PARO, "PARO OK");

    s_scr[p] = s;
    return s;
}

/* Cuerpo flexible debajo de la barra: ocupa todo el espacio restante, en
 * columna, con márgenes horizontal y vertical y separación "gap" entre filas. */
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

/* Espaciador que absorbe el espacio libre de una columna o fila. Sirve para
 * empujar el contenido siguiente (por ejemplo los botones) hacia abajo. */
static lv_obj_t *relleno(lv_obj_t *padre)
{
    lv_obj_t *r = caja(padre);
    lv_obj_set_width(r, 1);
    lv_obj_set_flex_grow(r, 1);
    return r;
}

/* ------------------------------------------------------- Eventos fijos */
/* Eventos que los botones envían. Son constantes porque LVGL guarda solo el
 * puntero (user data) y debe seguir siendo válido cuando se toque el botón. */
static const evento_t EV_CH1 = { .tipo = EV_SELECCIONAR_CANAL, .valor = CANAL_1 };
static const evento_t EV_CH2 = { .tipo = EV_SELECCIONAR_CANAL, .valor = CANAL_2 };
static const evento_t EV_VOLV = { .tipo = EV_VOLVER };
static const evento_t EV_INIC = { .tipo = EV_INICIAR };
static const evento_t EV_DET = { .tipo = EV_DETENER };
static const evento_t EV_REC = { .tipo = EV_RECONOCER };
static const evento_t EV_REI = { .tipo = EV_REINTENTAR };
/* Ajustes de frecuencia: el valor es el cambio en Hz, con signo */
static const evento_t EV_M100 = { .tipo = EV_AJUSTAR_FREF, .valor = -CH2_PASO_GRUESO_HZ };
static const evento_t EV_M10 = { .tipo = EV_AJUSTAR_FREF, .valor = -CH2_PASO_FINO_HZ };
static const evento_t EV_P10 = { .tipo = EV_AJUSTAR_FREF, .valor = CH2_PASO_FINO_HZ };
static const evento_t EV_P100 = { .tipo = EV_AJUSTAR_FREF, .valor = CH2_PASO_GRUESO_HZ };
/* Base de la frecuencia de conmutación del Canal 2, en Hz */
static const evento_t EV_F10K = { .tipo = EV_SELECCIONAR_FSW, .valor = 10000 };
static const evento_t EV_F20K = { .tipo = EV_SELECCIONAR_FSW, .valor = 20000 };

/* ------------------------------------------------------------ Pantallas */

/* ARRANQUE: título, barra animada y texto mientras se calibran los sensores */
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

/* ERROR SENSOR: una fila por sensor con su offset medido y si está en rango,
 * más el botón Reintentar. Solo se muestra con SENSORES_BLOQUEAN_INTERFAZ = 1. */
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
        /* Fila del sensor k: nombre, valor medido y marca OK / FUERA */
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

/* Tarjeta tocable para elegir un canal en la pantalla de ESPERA.
 *   color  borde de la tarjeta (color del canal)
 *   fondo  fondo de la tarjeta
 *   canal  título ("CANAL 1"), nombre ("Monofásico"), dos líneas de datos
 *          l1 y l2, y pie con la tensión del bus
 *   ev     evento que se envía al tocarla */
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
    etiqueta(b, FN20, C_TEXTO, canal);   /* título de la tarjeta: blanco, negrita 20 px */
    etiqueta(b, F16, C_TEXTO, nombre);
    etiqueta(b, F12, C_TEXTO_3, l1);
    etiqueta(b, F12, C_TEXTO_3, l2);
    relleno(b);
    etiqueta(b, F10, C_TEXTO_2, pie);
    lv_obj_add_event_cb(b, al_tocar, LV_EVENT_CLICKED, (void *)ev);
    return b;
}

/* ESPERA: menú principal con las dos tarjetas de canal lado a lado y una
 * línea de ayuda debajo */
static void crear_espera(void)
{
    lv_obj_t *s = crear_pantalla(P_ESPERA, C_FONDO);
    lv_obj_t *c = cuerpo(s, 10, 10, 6);
    lv_obj_t *fila = caja(c);
    lv_obj_set_width(fila, LV_PCT(100));
    lv_obj_set_flex_grow(fila, 1);
    lv_obj_set_flex_flow(fila, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(fila, 8, 0);
    tarjeta_canal(fila, C_AMBAR, 0xD98200, "CANAL 1", "Monofásico", "120 V · 60 Hz",
                  "SPWM unipolar", "Bus 200 VDC · 40 W", &EV_CH1);
    tarjeta_canal(fila, C_CIAN, 0x07ec, "CANAL 2", "Trifásico", "V/f · 10 a 1200 Hz",
                  "Motor A2212/13T", "Bus 12 a 24 VDC", &EV_CH2);
    s_esp.pie = etiqueta(c, F10, C_TEXTO_2, "Toque un canal para configurarlo");
    lv_obj_set_width(s_esp.pie, LV_PCT(100));
    lv_obj_set_style_text_align(s_esp.pie, LV_TEXT_ALIGN_CENTER, 0);
}

/* Fila inferior de las pantallas de configuración: botón Volver a la
 * izquierda e INICIAR que ocupa el resto del ancho. */
static lv_obj_t *fila_botones_inicio(lv_obj_t *padre)
{
    lv_obj_t *f = fila_flex(padre, LV_FLEX_FLOW_ROW, 8);
    boton(f, 96, 44, C_PANEL, C_BORDE_BTN, C_TEXTO, F14, "Volver", &EV_VOLV, false);
    lv_obj_t *b = boton(f, 100, 44, C_VERDE, 0, 0x0B1A12, F16, "INICIAR", &EV_INIC, false);
    lv_obj_set_flex_grow(b, 1);
    return f;
}

/* Encabezado de la pantalla de configuración: qué canal se configura */
static void titulo_canal(lv_obj_t *c, uint32_t color, const char *txt)
{
    lv_obj_t *l = etiqueta(c, FN16, color, txt);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_pad_bottom(l, 2, 0);
}

/* CONFIGURACION del Canal 1: parámetros fijos (solo informativos) y los
 * botones Volver / INICIAR */
static void crear_cfg1(void)
{
    lv_obj_t *s = crear_pantalla(P_CFG1, C_FONDO);
    lv_obj_t *c = cuerpo(s, 12, 8, 3);
    titulo_canal(c, C_AMBAR, "CANAL 1 · MONOFÁSICO");
    fila_dato(c, "Salida", "120 VRMS · 60 Hz", true, C_TEXTO_2, C_LINEA);
    fila_dato(c, "Índice de modulación", "0,88", true, C_TEXTO_2, C_LINEA);
    fila_dato(c, "Arranque suave", "0 a 0,88 en 1 s", true, C_TEXTO_2, C_LINEA);
    fila_dato(c, "Conmutación · tiempo muerto", "10 kHz · 800 ns", true, C_TEXTO_2, C_LINEA);
    fila_dato(c, "Umbral de sobrecorriente", "±2 A", false, C_TEXTO_2, C_LINEA);
    relleno(c);
    s_cfg1.aviso = etiqueta(c, F10, C_AMBAR, "");
    lv_obj_set_width(s_cfg1.aviso, LV_PCT(100));
    lv_obj_set_style_text_align(s_cfg1.aviso, LV_TEXT_ALIGN_CENTER, 0);
    fila_botones_inicio(c);
}

/* CONFIGURACION del Canal 2: frecuencia de referencia ajustable (botones
 * -100, -10, +10, +100), selector de la base de conmutación 10 / 20 kHz y
 * los botones Volver / INICIAR */
static void crear_cfg2(void)
{
    lv_obj_t *s = crear_pantalla(P_CFG2, C_FONDO);
    lv_obj_t *c = cuerpo(s, 12, 6, 6);

    /* Frecuencia de referencia en una sola fila: nombre a la izquierda,
     * valor a la derecha */
    lv_obj_t *cab = fila_flex(c, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_flex_align(cab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    etiqueta(cab, F14, C_TEXTO_2, "Frecuencia de referencia");
    s_cfg2.fref = etiqueta(cab, F28, C_TEXTO, "");

    /* Cuatro botones de ajuste con repetición al mantenerlos presionados */
    lv_obj_t *aj = fila_flex(c, LV_FLEX_FLOW_ROW, 6);
    const char *et[4] = { "−100", "−10", "+10", "+100" };
    const evento_t *ev[4] = { &EV_M100, &EV_M10, &EV_P10, &EV_P100 };
    for (int k = 0; k < 4; k++) {
        lv_obj_t *b = boton(aj, 60, 44, C_PANEL, C_BORDE_BTN, C_TEXTO, F14, et[k], ev[k], true);
        lv_obj_set_flex_grow(b, 1);
    }

    /* Selector de la base de conmutación: dos botones, uno queda marcado */
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
        /* El botón en estado "checked" (el elegido) se pinta de cian */
        lv_obj_set_style_bg_color(segs[k], lv_color_hex(C_CIAN), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(lv_obj_get_child(segs[k], 0), lv_color_hex(0x06212C), LV_STATE_CHECKED);
    }

    relleno(c);
    s_cfg2.aviso = etiqueta(c, F10, C_AMBAR, "");
    lv_obj_set_width(s_cfg2.aviso, LV_PCT(100));
    lv_obj_set_style_text_align(s_cfg2.aviso, LV_TEXT_ALIGN_CENTER, 0);
    fila_botones_inicio(c);
}

/* Fila de la lista de verificación de ARMADO: ícono (✓ o ✗) y texto. Por
 * "ico" y "lbl" devuelve los objetos para actualizarlos; lbl puede ser NULL. */
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

/* ARMADO: lista de tres verificaciones y barra con la cuenta de los 500 ms
 * antes de habilitar el canal */
static void crear_armado(void)
{
    lv_obj_t *s = crear_pantalla(P_ARMADO, C_FONDO);
    lv_obj_t *c = cuerpo(s, 16, 14, 8);
    s_arm.titulo = etiqueta(c, F16, C_TEXTO, "");
    lv_obj_t *ico_inactivo;   /* el ícono de esta fila no se actualiza, por eso no se guarda */
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

/* OPERACION del Canal 1: corriente RMS grande a la izquierda; f1, fsw y ma
 * a la derecha; barra de la rampa de ma y botón DETENER */
static void crear_op1(void)
{
    lv_obj_t *s = crear_pantalla(P_OP1, C_FONDO);
    lv_obj_t *c = cuerpo(s, 12, 8, 6);

    lv_obj_t *arriba = fila_flex(c, LV_FLEX_FLOW_ROW, 8);
    /* Tarjeta de la corriente */
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

    /* Columna de datos de la modulación */
    lv_obj_t *datos = caja(arriba);
    lv_obj_set_height(datos, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(datos, 1);
    lv_obj_set_flex_flow(datos, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(datos, 3, 0);
    s_op1.f1 = fila_dato(datos, "f1", "", true, C_TEXTO_2, C_LINEA);
    s_op1.fsw = fila_dato(datos, "fsw", "", true, C_TEXTO_2, C_LINEA);
    s_op1.ma = fila_dato(datos, "ma", "", false, C_TEXTO_2, C_LINEA);

    /* Barra del arranque suave con su estado ("subiendo" / "completa") */
    lv_obj_t *r = fila_flex(c, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    etiqueta(r, F10, C_TEXTO_2, "Rampa de ma");
    s_op1.rampa_txt = etiqueta(r, F10, C_TEXTO_2, "");
    s_op1.rampa = barra_progreso(c, 6, C_AMBAR);

    relleno(c);
    lv_obj_t *b = boton(c, 100, 48, C_ROJO, 0, 0xFFFFFF, F16, "DETENER", &EV_DET, false);
    lv_obj_set_width(b, LV_PCT(100));
}

/* Recuadro con una corriente RMS de fase: nombre pequeño arriba y valor
 * debajo. Devuelve la etiqueta del valor. */
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

/* OPERACION del Canal 2: f1 grande, fsw y ma a la derecha, barra de la
 * rampa de frecuencia, tres recuadros de corriente (U, V, W calculada) y
 * botones -10, +10 y DETENER */
static void crear_op2(void)
{
    lv_obj_t *s = crear_pantalla(P_OP2, C_FONDO);
    lv_obj_t *c = cuerpo(s, 10, 6, 5);

    /* Encabezado: f1 actual a la izquierda, fsw y ma a la derecha */
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

    /* Avance de f1 hacia la referencia */
    s_op2.rampa = barra_progreso(c, 6, C_CIAN);

    /* Corrientes RMS por fase */
    lv_obj_t *corr = fila_flex(c, LV_FLEX_FLOW_ROW, 6);
    s_op2.iu = caja_corriente(corr, "I_U RMS");
    s_op2.iv = caja_corriente(corr, "I_V RMS");
    s_op2.iw = caja_corriente(corr, "I_W RMS (calc.)");
    etiqueta(c, F10, C_TEXTO_2, "Umbral ±15 A por fase");

    relleno(c);
    /* Se puede ajustar la frecuencia en marcha con -10 y +10 */
    lv_obj_t *pie = fila_flex(c, LV_FLEX_FLOW_ROW, 6);
    boton(pie, 60, 44, C_PANEL, C_BORDE_BTN, C_TEXTO, F14, "−10", &EV_M10, true);
    boton(pie, 60, 44, C_PANEL, C_BORDE_BTN, C_TEXTO, F14, "+10", &EV_P10, true);
    lv_obj_t *b = boton(pie, 100, 44, C_ROJO, 0, 0xFFFFFF, F16, "DETENER", &EV_DET, false);
    lv_obj_set_flex_grow(b, 1);
}

/* FALLA: franja roja con el título y la causa, explicación de la causa,
 * dos datos del disparo y el botón para reconocer la falla */
static void crear_falla(void)
{
    lv_obj_t *s = crear_pantalla(P_FALLA, C_FONDO);

    /* Franja roja de borde a borde: lo primero que se ve */
    lv_obj_t *fr = caja(s);
    lv_obj_set_size(fr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(fr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fr, lv_color_hex(C_ROJO), 0);
    lv_obj_set_style_pad_hor(fr, 12, 0);
    lv_obj_set_style_pad_ver(fr, 5, 0);
    lv_obj_set_flex_flow(fr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(fr, 1, 0);
    etiqueta(fr, F16, 0xFFFFFF, SIMB_ALERTA "  FALLA · SALIDA APAGADA");
    s_falla.causa = etiqueta(fr, F14, 0xFFFFFF, "");

    lv_obj_t *c = cuerpo(s, 12, 6, 5);

    /* Qué pasó y qué revisar */
    s_falla.detalle = etiqueta(c, F12, C_TEXTO, "");
    lv_obj_set_width(s_falla.detalle, LV_PCT(100));
    lv_label_set_long_mode(s_falla.detalle, LV_LABEL_LONG_WRAP);

    /* Datos del disparo */
    lv_obj_t *tabla = fila_flex(c, LV_FLEX_FLOW_COLUMN, 1);
    s_falla.pico = fila_dato(tabla, "Corriente medida / límite", "", true, C_TEXTO, C_LINEA);
    s_falla.canal = fila_dato(tabla, "Canal y frecuencia", "", false, C_TEXTO, C_LINEA);

    relleno(c);
    s_falla.aviso = etiqueta(c, F10, C_AMBAR, "");
    lv_obj_set_width(s_falla.aviso, LV_PCT(100));
    lv_obj_set_style_text_align(s_falla.aviso, LV_TEXT_ALIGN_CENTER, 0);
    s_falla.boton = boton(c, 100, 40, C_AMBAR, 0, 0x000000, F14, "ACEPTAR Y VOLVER AL INICIO", &EV_REC, false);
    lv_obj_set_width(s_falla.boton, LV_PCT(100));
}

/* Explicación de cada causa: qué pasó y qué revisar */
static const char *detalle_falla(causa_falla_t c)
{
    switch (c) {
    case CAUSA_SOBRECORRIENTE_CH1:
        return "Corriente de salida sobre el límite. Revise cortocircuitos o exceso de carga.";
    case CAUSA_SOBRECORRIENTE_U:
    case CAUSA_SOBRECORRIENTE_V:
    case CAUSA_SOBRECORRIENTE_W:
        return "Corriente de fase sobre el límite. Revise cortocircuitos o motor frenado.";
    case CAUSA_PARO:
        return "Se presionó el paro o se soltó su cable. Drivers apagados por hardware.";
    case CAUSA_ERROR_INTERNO:
        return "Otro canal apareció habilitado o el PWM no arrancó. Revise EN y SD.";
    default:
        return "";
    }
}

/* ----------------------------------------------------------- Público */

/* Crea las nueve pantallas y muestra la de ARRANQUE. "cb" recibe los
 * eventos de todos los botones. */
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

/* Qué pantalla corresponde al estado de la máquina. En CONFIGURACION y
 * OPERACION depende además del canal elegido. */
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

/* Actualiza la barra superior de la pantalla p: color y nombre del estado,
 * texto central y estado del paro de emergencia. */
static void actualizar_barra(pantalla_t p, const ui_datos_t *d)
{
    const sm_t *sm = d->sm;
    barra_t *b = &s_barra[p];
    /* Color de la cápsula según el estado (gris por defecto) */
    uint32_t fondo = C_LINEA, txt = C_TEXTO;
    switch (sm->estado) {
    case EST_ERROR_SENSOR: fondo = 0xB02400; txt = 0xFFFFFF; break;
    case EST_ARMADO:       fondo = 0x805400; txt = 0xFFFFFF; break;
    case EST_OPERACION:    fondo = 0x007030; txt = 0xFFFFFF; break;
    case EST_FALLA:        fondo = C_ROJO;   txt = 0xFFFFFF; break;
    default: break;
    }
    lv_obj_set_style_bg_color(b->pastilla, lv_color_hex(fondo), 0);
    lv_obj_set_style_text_color(b->pastilla_txt, lv_color_hex(txt), 0);
    texto(b->pastilla_txt, "%s", sm_nombre_estado(sm->estado));

    /* Texto central: en falla, "Drivers apagados"; con canal elegido, su
     * nombre en el color del canal; si no, el mensaje que corresponda */
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

    /* Indicador del paro: verde si está libre, rojo si está activo. En la
     * pantalla de falla se distingue "PARO LIBERADO" de "PARO ACTIVO". */
    uint32_t c_paro = d->paro ? C_ROJO_TXT : C_VERDE_PARO;
    if (sm->estado == EST_FALLA) {
        c_paro = d->paro ? 0xFFFFFF : C_VERDE_PARO;
        texto(b->paro, d->paro ? "PARO ACTIVO" : "PARO LIBERADO");
    } else {
        texto(b->paro, d->paro ? "PARO ACTIVO" : "PARO OK");
    }
    lv_obj_set_style_text_color(b->paro, lv_color_hex(c_paro), 0);
    lv_obj_set_style_bg_color(b->punto, lv_color_hex(c_paro), 0);
}

/* Muestra el aviso de la máquina de estados si lo hay; si no, un texto
 * por defecto. */
static void aviso_o(lv_obj_t *lbl, const sm_t *sm, const char *defecto)
{
    texto(lbl, "%s", sm->aviso ? sm->aviso : defecto);
}

/* Se llama cada 50 ms. Cambia de pantalla si el estado lo pide y refresca
 * solo los textos y colores que dependen de los datos. */
void ui_actualizar(const ui_datos_t *d)
{
    const sm_t *sm = d->sm;
    char a[16], b[16];   /* buffers temporales para num() */
    pantalla_t p = pantalla_de(sm);
    if (p != s_actual) {
        lv_screen_load(s_scr[p]);
        s_actual = p;
    }
    actualizar_barra(p, d);

    switch (p) {
    /* ARRANQUE: la barra avanza en bucle como indicador de actividad */
    case P_ARRANQUE:
        lv_bar_set_value(s_arr.barra_cal, (int32_t)((d->ahora_ms / 4) % 100), LV_ANIM_OFF);
        break;

    /* ERROR SENSOR: offset de cada sensor en V, con OK o FUERA y la fila
     * resaltada si está fuera de rango */
    case P_ERROR:
        for (int k = 0; k < SENSOR_NUM; k++) {
            bool ok = d->med.offset_ok[k];
            texto(s_err.valor[k], "%s V", num(a, sizeof a, d->med.offset_mv[k] / 1000.0f, 3));
            texto(s_err.marca[k], ok ? "OK" : "FUERA");
            lv_obj_set_style_text_color(s_err.marca[k], lv_color_hex(ok ? C_VERDE : C_ROJO_TXT), 0);
            lv_obj_set_style_bg_color(s_err.fila[k], lv_color_hex(ok ? C_PANEL : 0x401000), 0);
            lv_obj_set_style_border_width(s_err.fila[k], ok ? 0 : 1, 0);
            lv_obj_set_style_border_color(s_err.fila[k], lv_color_hex(0xFF3400), 0);
        }
        break;

    /* ESPERA: texto de ayuda, o el aviso del paro si está presionado */
    case P_ESPERA:
        aviso_o(s_esp.pie, sm, d->paro ? "Paro presionado: libérelo para operar" : "Toque un canal para configurarlo");
        break;

    case P_CFG1:
        aviso_o(s_cfg1.aviso, sm, "Conecte el bus de 200 V y desconecte el bus del Canal 2");
        break;

    /* CONFIGURACION del Canal 2: valor de la frecuencia y botón de la base
     * de conmutación marcado según el parámetro elegido */
    case P_CFG2: {
        texto(s_cfg2.fref, "%u Hz", (unsigned)sm->par.f_ref_hz);
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

    /* ARMADO: t es el tiempo transcurrido de la espera, limitado a 500 ms */
    case P_ARMADO: {
        uint32_t t = d->ahora_ms - sm->t_entrada_ms;
        if (t > ARMADO_ESPERA_MS) t = ARMADO_ESPERA_MS;
        bool ch1 = (sm->par.canal == CANAL_1);
        texto(s_arm.titulo, "Armando Canal %d…", ch1 ? 1 : 2);
        texto(s_arm.txt_inactivo, "Canal %d con SD en alto (deshabilitado)", ch1 ? 2 : 1);
        /* Ícono del paro: ✓ verde si está libre, ✗ rojo si está presionado */
        texto(s_arm.ico_paro, "%s", d->paro ? SIMB_X : SIMB_OK);
        lv_obj_set_style_text_color(s_arm.ico_paro, lv_color_hex(d->paro ? C_ROJO_TXT : C_VERDE), 0);
        /* Ícono del tiempo: ámbar mientras corre la espera, verde al terminar */
        bool listo = (t >= ARMADO_ESPERA_MS);
        lv_obj_set_style_text_color(s_arm.ico_tiempo, lv_color_hex(listo ? C_VERDE : C_AMBAR), 0);
        lv_bar_set_value(s_arm.progreso, (int32_t)(t * 100 / ARMADO_ESPERA_MS), LV_ANIM_OFF);
        texto(s_arm.restante, "Habilitando en %s s", num(a, sizeof a, (ARMADO_ESPERA_MS - t) / 1000.0f, 1));
        texto(s_arm.derecha, ch1 ? "ma inicial 0" : "f1 inicial 10 Hz");
        break;
    }

    /* OPERACION del Canal 1 */
    case P_OP1: {
        texto(s_op1.irms, "%s A", num(a, sizeof a, d->med.rms_a[SENSOR_CH1], 2));
        texto(s_op1.f1, "%u Hz", (unsigned)lroundf(d->pot.f1_hz));
        texto(s_op1.fsw, "%s kHz", num(a, sizeof a, d->pot.fsw_hz / 1000.0f, 1));
        texto(s_op1.ma, "%s", num(b, sizeof b, d->pot.ma, 2));
        /* La barra muestra ma como porcentaje de su valor final */
        lv_bar_set_value(s_op1.rampa, (int32_t)lroundf(100.0f * d->pot.ma / CH1_MA_FINAL), LV_ANIM_OFF);
        texto(s_op1.rampa_txt, d->pot.rampa_completa ? "completa" : "subiendo");
        break;
    }

    /* OPERACION del Canal 2 */
    case P_OP2: {
        texto(s_op2.subt, "f1 actual · ref %u Hz", (unsigned)sm->par.f_ref_hz);
        texto(s_op2.f1, "%u Hz", (unsigned)lroundf(d->pot.f1_hz));
        texto(s_op2.fsw, "fsw %s kHz", num(a, sizeof a, d->pot.fsw_hz / 1000.0f, 1));
        texto(s_op2.ma, "ma %s", num(b, sizeof b, d->pot.ma, 2));
        /* La barra muestra f1 como porcentaje de la referencia (máximo 100) */
        float ref = d->pot.f_ref_hz > 0 ? d->pot.f_ref_hz : 1.0f;
        int32_t pr = (int32_t)lroundf(100.0f * d->pot.f1_hz / ref);
        lv_bar_set_value(s_op2.rampa, pr > 100 ? 100 : pr, LV_ANIM_OFF);
        /* Índices de rms_a: 1 = U, 2 = V, 3 = W calculada */
        texto(s_op2.iu, "%s A", num(a, sizeof a, d->med.rms_a[1], 2));
        texto(s_op2.iv, "%s A", num(a, sizeof a, d->med.rms_a[2], 2));
        texto(s_op2.iw, "%s A", num(a, sizeof a, d->med.rms_a[3], 2));
        break;
    }

    /* FALLA: causa, explicación, datos del disparo y estado del botón */
    case P_FALLA: {
        texto(s_falla.causa, "%s", sm_texto_causa(sm->causa));
        texto(s_falla.detalle, "%s", detalle_falla(sm->causa));
        /* Solo las sobrecorrientes tienen un pico y un límite que mostrar */
        bool por_corriente = (sm->causa >= CAUSA_SOBRECORRIENTE_CH1 && sm->causa <= CAUSA_SOBRECORRIENTE_W);
        if (por_corriente) {
            texto(s_falla.pico, "%s A / %s", num(a, sizeof a, fabsf(sm->pico_a), 1),
                  sm->causa == CAUSA_SOBRECORRIENTE_CH1 ? "±2 A" : "±15 A");
        } else {
            texto(s_falla.pico, "No aplica");
        }
        if (sm->canal_falla == CANAL_NINGUNO) {
            texto(s_falla.canal, "-");
        } else {
            texto(s_falla.canal, "Canal %d · %d Hz", (int)sm->canal_falla, (int)sm->f1_falla_hz);
        }
        /* Con el paro presionado el botón queda deshabilitado: no se puede
         * reconocer la falla hasta soltarlo */
        if (d->paro) {
            texto(s_falla.aviso, "Suelte el paro de emergencia para continuar");
            lv_obj_add_state(s_falla.boton, LV_STATE_DISABLED);
        } else {
            aviso_o(s_falla.aviso, sm, "Corrija la causa antes de volver a encender");
            lv_obj_remove_state(s_falla.boton, LV_STATE_DISABLED);
        }
        break;
    }

    default:
        break;
    }
}
