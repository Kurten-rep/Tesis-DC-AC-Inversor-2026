/*
 * config.h
 * Parámetros del firmware. Cada valor indica de dónde sale.
 * [PENDIENTE] = valor inicial que se debe ajustar en las pruebas.
 * [VERIFICAR] = depende del hardware armado.
 *
 * Este archivo reúne todo lo que se ajusta sin tocar la lógica: tiempos
 * del MCPWM, frecuencias de cada canal, sensado, pantalla y táctil.
 * Los comentarios con "sección" o "Tabla" remiten al documento de tesis.
 */
#pragma once

/* ---------------------------------------------------------------- MCPWM */
/* Reloj de los temporizadores del MCPWM. A 80 MHz una cuenta dura
 * 1 / 80 MHz = 12,5 ns, y de ahí salen todos los tiempos en "cuentas". */
#define MCPWM_RESOLUCION_HZ     80000000u  /* 12,5 ns por cuenta (sección 4.3.8) */
/* Tiempo muerto entre apagar un MOSFET y encender el complementario de la
 * misma rama, para evitar un cortocircuito (shoot-through).
 * 64 cuentas x 12,5 ns = 800 ns; 48 cuentas x 12,5 ns = 600 ns. */
#define TM_CH1_CUENTAS          64u        /* 800 ns, Tabla 14 */
#define TM_CH2_CUENTAS          48u        /* 600 ns, Tabla 14 */
/* Ciclo de trabajo máximo del lado alto, en milésimas (950 = 95 %). El 5 %
 * restante deja tiempo para recargar el condensador bootstrap del driver. */
#define CICLO_MAX_PERMIL        950u       /* 95 %, recarga del bootstrap */

/* ------------------------------------------------------------ Canal 1 */
#define CH1_F1_HZ               60u        /* frecuencia de salida fija */
#define CH1_FSW_HZ              10000u     /* frecuencia de conmutación */
#define CH1_MA_FINAL            0.88f      /* sección 4.3.4, compensa el tiempo muerto */
#define CH1_RAMPA_MA_MS         1000u      /* 0 a 0,88 en 1 s */
#define CH1_UMBRAL_A            2.0f       /* pico de corriente que dispara la protección */

/* ------------------------------------------------------------ Canal 2 */
#define CH2_F1_MIN_HZ           10u        /* frecuencia de referencia mínima */
#define CH2_F1_MAX_HZ           1200u      /* frecuencia de referencia máxima */
#define CH2_F1_DEFECTO_HZ       300u       /* valor inicial en la pantalla */
#define CH2_FSW_BASE_DEFECTO    10000u     /* el motor arranca con la base de 10 kHz */
#define CH2_FSW_MAX_HZ          25200u     /* ecuación 23: 21 x 1200 Hz */
#define CH2_MF_SINCRONO         21u        /* relación fsw / f1 en modo síncrono */
#define CH2_MA0                 0.05f      /* [PENDIENTE] incremento a baja frecuencia */
#define CH2_PENDIENTE_HZ_S      100.0f     /* [PENDIENTE] rampa de aceleración */
#define CH2_UMBRAL_A            15.0f      /* pico de corriente que dispara la protección */
#define CH2_PASO_FINO_HZ        10         /* botones +/- pequeños de la frecuencia */
#define CH2_PASO_GRUESO_HZ      100        /* botones +/- grandes de la frecuencia */
#define CH2_POLOS               14u        /* 7 pares, para mostrar rpm */

/* ---------------------------------------------------------- Secuencias */
#define ARMADO_ESPERA_MS        500u       /* compuertas apagadas en cada cambio de canal */
#define PERIODO_INTERFAZ_MS     50u        /* cada cuánto corre la tarea de interfaz */
#define PERIODO_RAMPA_MS        1u         /* cada cuánto corre la tarea de rampa */

/* ------------------------------------------------------------- Sensado */
/* ACS712-20A: 100 mV/A [19]. Divisor 10 kΩ / 20 kΩ, 1 %, ±50 ppm/°C: 2/3.
 * El divisor baja la salida del sensor (0 a 5 V) al rango del ADC (0 a 3,3 V).
 * [VERIFICAR] CONTEXTO_PROYECTO.md deja abierto si el sensor de CH1 es el
 * 05B (185 mV/A); si lo es, cambiar SENS_CH1_MV_A a 185.0f. */
#define SENS_CH1_MV_A           100.0f     /* sensibilidad del sensor del Canal 1, mV por A */
#define SENS_CH2_MV_A           100.0f     /* sensibilidad de los sensores del Canal 2, mV por A */
#define DIVISOR_RELACION        (20.0f / 30.0f)
/* Con corriente cero el sensor entrega la mitad de la alimentación (2,5 V),
 * que tras el divisor son 1,67 V. Si el offset medido cae fuera de este
 * rango, el sensor está desconectado o dañado. */
#define OFFSET_MIN_MV           1570       /* rango válido del offset en el pin del ADC */
#define OFFSET_MAX_MV           1770
#define CALIBRACION_MS          200u       /* duración de la medición del offset */

/* ADC1 continuo: 3 canales a 60 kS/s en total, 20 kS/s por canal.
 * El ESP32-S3 admite hasta 83,3 kS/s (SOC_ADC_SAMPLE_FREQ_THRES_HIGH). */
#define ADC_FS_TOTAL_HZ         60000u
#define ADC_NUM_CANALES         3u
#define ADC_FS_CANAL_HZ         (ADC_FS_TOTAL_HZ / ADC_NUM_CANALES)
#define ADC_BYTES_TRAMA         240u       /* 60 conversiones: 1 ms por trama */
#define RMS_VENTANA_MIN_S       0.1f       /* ventana mínima para calcular el valor eficaz */

/* --------------------------------------------- Modo de pruebas del MCU */
/* [PRUEBAS] 1 = una falla en la calibración de los sensores bloquea la
 *               interfaz en ERROR SENSOR (comportamiento final del equipo).
 *           0 = la interfaz sigue funcionando y se puede recorrer todo el
 *               flujo; los drivers nunca se habilitan (EN inactivo) y la
 *               protección por sobrecorriente no actúa, porque sin offset
 *               válido la lectura no significa nada. El paro (GPIO 7)
 *               sigue frenando el MCPWM por hardware.
 * Volver a 1 antes de conectar la etapa de potencia. */
#define SENSORES_BLOQUEAN_INTERFAZ  1

/* -------------------------------------------- Protecciones desactivables */
/* [PRUEBAS] Paro de emergencia en GPIO 7.
 *   1 = el pin frena el MCPWM por hardware y lleva el equipo a FALLA.
 *   0 = no se usa: GPIO 7 no se configura, no frena nada y la interfaz
 *       muestra "SIN PARO" en la barra superior. */
#ifndef USAR_PARO_EMERGENCIA
#define USAR_PARO_EMERGENCIA        0
#endif

/* [PRUEBAS] Protección por sobrecorriente con los ACS712.
 *   1 = una corriente sobre CH1_UMBRAL_A o CH2_UMBRAL_A frena el canal.
 *   0 = no se compara contra los umbrales; los sensores solo sirven para
 *       mostrar la corriente. Una calibración fallida tampoco bloquea:
 *       se avisa en pantalla y los drivers se habilitan igual. */
#ifndef USAR_PROTECCION_SOBRECORRIENTE
#define USAR_PROTECCION_SOBRECORRIENTE  0
#endif

/* ----------------------------------------------------------- Pantalla */
#define LCD_H_RES               320        /* resolución horizontal, en píxeles */
#define LCD_V_RES               240        /* resolución vertical, en píxeles */
#define LCD_SPI_HZ              (10 * 1000 * 1000)  /* reloj SPI de la pantalla */
#define LCD_LINEAS_BUFFER       24         /* líneas del buffer de dibujo de LVGL */
/* [VERIFICAR] orientación del módulo de 2,8": se ajusta con estos tres. */
#define LCD_INTERCAMBIAR_XY     0          /* 1 = girar 90° (intercambia X e Y) */
#define LCD_ESPEJO_X            1          /* 1 = invertir horizontalmente */
#define LCD_ESPEJO_Y            0          /* 1 = invertir verticalmente */
#define LCD_ORDEN_BGR           0          /* 1 = el panel usa orden de color BGR */
/* Perfil de potencia, VCOM y gamma del panel (mismos números de prueba_tft).
 * La temporización y el barrido son siempre los del driver de Espressif.
 *   1 = Espressif (por defecto)        2 = Adafruit / TFT_eSPI
 *   3 = ejemplo antiguo de ESP-IDF     5 = Adafruit con VCOM alto
 *   6 = ESP-IDF antiguo con VCOM alto
 * El 4 (fábrica) no se ofrece: con él la imagen sale entrelazada. */
#define LCD_PERFIL              2

#define TACTIL_SPI_HZ           (2 * 1000 * 1000)  /* reloj SPI del XPT2046 */
/* [VERIFICAR] calibración del XPT2046. Con TACTIL_DEPURAR en 1 se imprimen
 * las lecturas crudas en el monitor serie para ajustar estos valores.
 * El XPT2046 entrega valores de 12 bits (0 a 4095); MIN y MAX son las
 * lecturas crudas en los bordes de la pantalla. */
#define TACTIL_X_MIN            300
#define TACTIL_X_MAX            3800
#define TACTIL_Y_MIN            300
#define TACTIL_Y_MAX            3800
#define TACTIL_INTERCAMBIAR_XY  1          /* 1 = el eje X del táctil es el Y de la pantalla */
#define TACTIL_INVERTIR_X       1          /* 1 = invertir el eje X del táctil */
#define TACTIL_INVERTIR_Y       1          /* 1 = invertir el eje Y del táctil */
#define TACTIL_Z_UMBRAL         400        /* presión mínima para contar un toque */
#define TACTIL_DEPURAR          0          /* 1 = imprimir lecturas crudas por consola */

/* ------------------------------------------------------ Modo de banco */
/* 1: mediciones por consola para probar el MCU sin drivers (PRUEBA_BANCO.md).
 * Se activa con  idf.py -B build_banco -DMODO_BANCO=1 build  */
#ifndef MODO_BANCO
#define MODO_BANCO              0
#endif

/* Nivel lógico de EN que habilita el canal. [VERIFICAR] con el esquemático
 * de la tarjeta de control: EN en alto lleva SD a nivel bajo. */
#define EN_NIVEL_ACTIVO         1
