/*
 * pines.h
 * Asignación de pines del ESP32-S3-N16R8 (Tabla 15 del documento y
 * CONTEXTO_PROYECTO.md, sección 6).
 */
#pragma once

/* Sensado de corriente, ADC1 (ADC2 choca con el Wi-Fi) */
#define PIN_I_CH1        1   /* ADC1_CH0 */
#define PIN_I_U          2   /* ADC1_CH1 */
#define PIN_I_V          4   /* ADC1_CH3 */
#define ADC_CANAL_I_CH1  0
#define ADC_CANAL_I_U    1
#define ADC_CANAL_I_V    3

/* Habilitación de drivers. EN activo lleva a nivel bajo la entrada SD
 * del canal. Las SD tienen pull-up, así que con el ESP32-S3 apagado o en
 * arranque los drivers quedan deshabilitados. */
#define PIN_EN_CH1       5
#define PIN_EN_CH2       6

/* Paro de emergencia: NC a tierra con pull-up externo. Presionado o cable
 * cortado = nivel alto. También es la entrada de falla de los dos MCPWM. */
#define PIN_PARO         7

/* Pantalla ILI9341 y táctil XPT2046, bus SPI2 compartido */
#define PIN_T_IRQ        8   /* opcional, no se usa: se lee la presión Z */
#define PIN_TFT_DC       9
#define PIN_TFT_CS       10
#define PIN_SPI_MOSI     11
#define PIN_SPI_SCK      12
#define PIN_SPI_MISO     13  /* solo T_DO; el SDO del TFT queda sin conectar */
#define PIN_T_CS         14
/* TFT_RST va al pin EN de la placa y TFT_BL a 3,3 V: no usan GPIO. */

/* Canal 1, MCPWM0 (grupo 0) */
#define PIN_HIN_A        15  /* operador 0, generador A */
#define PIN_LIN_A        16  /* operador 0, generador B */
#define PIN_HIN_B        17  /* operador 1, generador A */
#define PIN_LIN_B        18  /* operador 1, generador B */

/* Canal 2, MCPWM1 (grupo 1) */
#define PIN_HIN_U        39
#define PIN_LIN_U        40
#define PIN_HIN_V        41
#define PIN_LIN_V        42
#define PIN_HIN_W        47
#define PIN_LIN_W        21
