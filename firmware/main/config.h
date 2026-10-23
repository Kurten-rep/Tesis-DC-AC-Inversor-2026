/*
 * config.h
 * Parámetros del firmware. Cada valor indica de dónde sale.
 * [PENDIENTE] = valor inicial que se debe ajustar en las pruebas.
 * [VERIFICAR] = depende del hardware armado.
 */
#pragma once

/* ---------------------------------------------------------------- MCPWM */
#define MCPWM_RESOLUCION_HZ     80000000u  /* 12,5 ns por cuenta (sección 4.3.8) */
#define TM_CH1_CUENTAS          64u        /* 800 ns, Tabla 14 */
#define TM_CH2_CUENTAS          48u        /* 600 ns, Tabla 14 */
#define CICLO_MAX_PERMIL        950u       /* 95 %, recarga del bootstrap */

/* ------------------------------------------------------------ Canal 1 */
#define CH1_F1_HZ               60u
#define CH1_FSW_HZ              20000u
#define CH1_MA_FINAL            0.88f      /* sección 4.3.4, compensa el tiempo muerto */
#define CH1_RAMPA_MA_MS         1000u      /* 0 a 0,88 en 1 s */
#define CH1_UMBRAL_A            2.0f

/* ------------------------------------------------------------ Canal 2 */
#define CH2_F1_MIN_HZ           10u
#define CH2_F1_MAX_HZ           1200u
#define CH2_F1_DEFECTO_HZ       300u
#define CH2_FSW_BASE_DEFECTO    10000u     /* el motor arranca con la base de 10 kHz */
#define CH2_FSW_MAX_HZ          25200u     /* ecuación 23: 21 x 1200 Hz */
#define CH2_MF_SINCRONO         21u
#define CH2_MA0                 0.05f      /* [PENDIENTE] incremento a baja frecuencia */
#define CH2_PENDIENTE_HZ_S      100.0f     /* [PENDIENTE] rampa de aceleración */
#define CH2_UMBRAL_A            15.0f
#define CH2_PASO_FINO_HZ        10
#define CH2_PASO_GRUESO_HZ      100
#define CH2_POLOS               14u        /* 7 pares, para mostrar rpm */

/* ---------------------------------------------------------- Secuencias */
#define ARMADO_ESPERA_MS        500u       /* compuertas apagadas en cada cambio de canal */
#define PERIODO_INTERFAZ_MS     50u
#define PERIODO_RAMPA_MS        1u

/* ------------------------------------------------------------- Sensado */
/* ACS712-20A: 100 mV/A [19]. Divisor 10 kΩ / 20 kΩ, 1 %, ±50 ppm/°C: 2/3.
 * [VERIFICAR] CONTEXTO_PROYECTO.md deja abierto si el sensor de CH1 es el
 * 05B (185 mV/A); si lo es, cambiar SENS_CH1_MV_A a 185.0f. */
#define SENS_CH1_MV_A           100.0f
#define SENS_CH2_MV_A           100.0f
#define DIVISOR_RELACION        (20.0f / 30.0f)
#define OFFSET_MIN_MV           1570       /* rango válido del offset en el pin del ADC */
#define OFFSET_MAX_MV           1770
#define CALIBRACION_MS          200u

/* ADC1 continuo: 3 canales a 60 kS/s en total, 20 kS/s por canal.
 * El ESP32-S3 admite hasta 83,3 kS/s (SOC_ADC_SAMPLE_FREQ_THRES_HIGH). */
#define ADC_FS_TOTAL_HZ         60000u
#define ADC_NUM_CANALES         3u
#define ADC_FS_CANAL_HZ         (ADC_FS_TOTAL_HZ / ADC_NUM_CANALES)
#define ADC_BYTES_TRAMA         240u       /* 60 conversiones: 1 ms por trama */
#define RMS_VENTANA_MIN_S       0.1f

/* ----------------------------------------------------------- Pantalla */
#define LCD_H_RES               320
#define LCD_V_RES               240
#define LCD_SPI_HZ              (40 * 1000 * 1000)
#define LCD_LINEAS_BUFFER       24
/* [VERIFICAR] orientación del módulo de 2,8": se ajusta con estos tres. */
#define LCD_INTERCAMBIAR_XY     1
#define LCD_ESPEJO_X            1
#define LCD_ESPEJO_Y            0
#define LCD_ORDEN_BGR           1

#define TACTIL_SPI_HZ           (2 * 1000 * 1000)
/* [VERIFICAR] calibración del XPT2046. Con TACTIL_DEPURAR en 1 se imprimen
 * las lecturas crudas en el monitor serie para ajustar estos valores. */
#define TACTIL_X_MIN            300
#define TACTIL_X_MAX            3800
#define TACTIL_Y_MIN            300
#define TACTIL_Y_MAX            3800
#define TACTIL_INTERCAMBIAR_XY  1
#define TACTIL_INVERTIR_X       0
#define TACTIL_INVERTIR_Y       1
#define TACTIL_Z_UMBRAL         400
#define TACTIL_DEPURAR          0

/* ------------------------------------------------------ Modo de banco */
/* 1: mediciones por consola para probar el MCU sin drivers (PRUEBA_BANCO.md).
 * Se activa con  idf.py -B build_banco -DMODO_BANCO=1 build  */
#ifndef MODO_BANCO
#define MODO_BANCO              0
#endif

/* Nivel lógico de EN que habilita el canal. [VERIFICAR] con el esquemático
 * de la tarjeta de control: EN en alto lleva SD a nivel bajo. */
#define EN_NIVEL_ACTIVO         1
