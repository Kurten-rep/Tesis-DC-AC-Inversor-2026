/*
 * pines.h
 * Asignación de pines del ESP32-S3-N16R8 (Tabla 15 del documento y
 * CONTEXTO_PROYECTO.md, sección 6).
 *
 * Todos los números son GPIO del chip, no el número impreso de la placa.
 * Este es el único archivo que se debe editar si se cambia una conexión.
 * Aquí solo hay definiciones: no genera código ni depende de ESP-IDF.
 */
#pragma once

/* ------------------------------------------------ Sensado de corriente */
/* Se usa ADC1 porque ADC2 choca con el Wi-Fi. Cada sensor tiene dos números:
 * el GPIO donde se conecta el cable (PIN_*) y el canal interno del ADC1 que
 * le corresponde en el ESP32-S3 (ADC_CANAL_*). Los dos deben ser coherentes:
 * GPIO1 = ADC1_CH0, GPIO2 = ADC1_CH1, GPIO4 = ADC1_CH3. */
#define PIN_I_CH1        1   /* ADC1_CH0: corriente del Canal 1 (monofásico) */
#define PIN_I_U          2   /* ADC1_CH1: corriente de la fase U del Canal 2 */
#define PIN_I_V          4   /* ADC1_CH3: corriente de la fase V del Canal 2 */
#define ADC_CANAL_I_CH1  0
#define ADC_CANAL_I_U    1
#define ADC_CANAL_I_V    3
/* La fase W no tiene sensor propio: su corriente se calcula como
 * I_W = -(I_U + I_V) en medicion.c. */

/* ------------------------------------------- Habilitación de drivers */
/* EN activo lleva a nivel bajo la entrada SD del canal (la lógica exacta
 * depende de EN_NIVEL_ACTIVO en config.h). Las SD tienen pull-up, así que
 * con el ESP32-S3 apagado o en arranque los drivers quedan deshabilitados:
 * es el estado seguro por hardware. */
#define PIN_EN_CH1       5   /* habilita los drivers del Canal 1 */
#define PIN_EN_CH2       6   /* habilita los drivers del Canal 2 */

/* ---------------------------------------------- Paro de emergencia */
/* Pulsador NC a tierra con pull-up externo. Presionado o cable cortado =
 * nivel alto = paro activo. Es a la vez un GPIO de lectura y la entrada de
 * falla de los dos MCPWM, por lo que frena las salidas por hardware sin
 * pasar por el software. */
#define PIN_PARO         7

/* -------------------------------- Pantalla ILI9341 y táctil XPT2046 */
/* Los dos comparten el bus SPI2 (MOSI, SCK, MISO) y se distinguen por su
 * línea CS. */
#define PIN_T_IRQ        8   /* opcional, no se usa: se lee la presión Z */
#define PIN_TFT_DC       9   /* datos/comando del ILI9341 */
#define PIN_TFT_CS       10  /* selección del ILI9341 */
#define PIN_SPI_MOSI     11  /* datos hacia la pantalla y el táctil */
#define PIN_SPI_SCK      12  /* reloj SPI */
#define PIN_SPI_MISO     13  /* solo T_DO; el SDO del TFT queda sin conectar */
#define PIN_T_CS         14  /* selección del XPT2046 */
/* TFT_RST va al pin EN de la placa y TFT_BL a 3,3 V: no usan GPIO. */

/* ---------------------------------------- Canal 1, MCPWM0 (grupo 0) */
/* Puente completo monofásico: dos ramas (A y B), cada una con un lado alto
 * (HIN) y uno bajo (LIN). Cada rama usa un operador del MCPWM. */
#define PIN_HIN_A        15  /* operador 0, generador A */
#define PIN_LIN_A        16  /* operador 0, generador B */
#define PIN_HIN_B        17  /* operador 1, generador A */
#define PIN_LIN_B        18  /* operador 1, generador B */

/* ---------------------------------------- Canal 2, MCPWM1 (grupo 1) */
/* Puente trifásico: tres ramas (U, V, W), un operador del MCPWM por rama. */
#define PIN_HIN_U        39
#define PIN_LIN_U        40
#define PIN_HIN_V        41
#define PIN_LIN_V        42
#define PIN_HIN_W        47
#define PIN_LIN_W        21

/* ----------------------------------------------------- Depuración */
/* Solo en modo de banco: queda en alto mientras corre la ISR de la SPWM,
 * para medir su duración con el osciloscopio.
 * [VERIFICAR] usar el GPIO 38 o 48 que no tenga el LED RGB; si toca el LED,
 * el LED parpadea sin consecuencias. */
#define PIN_DEPURACION   38
