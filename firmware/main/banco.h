/*
 * banco.h
 * Modo de banco: mediciones del MCU sin drivers conectados.
 * Se activa compilando con  idf.py -B build_banco -DMODO_BANCO=1 build
 *
 * Con MODO_BANCO = 0 todas las funciones quedan vacías y no agregan código.
 */
#pragma once
#include <stdint.h>
#include "config.h"

#if MODO_BANCO
#include "esp_attr.h"
#include "esp_cpu.h"
#include "driver/gpio.h"
#include "pines.h"

typedef struct {
    uint32_t llamadas;
    uint64_t ciclos_suma;     /* tiempo dentro de la ISR */
    uint32_t ciclos_max;
    uint32_t periodo_min;     /* ciclos entre entradas consecutivas */
    uint32_t periodo_max;
} banco_isr_t;

extern banco_isr_t g_banco_isr[2];
extern uint32_t g_banco_t_ant[2];

/* Se llama al entrar a la ISR del grupo g. Devuelve la marca de tiempo. */
static inline IRAM_ATTR uint32_t banco_isr_entrada(int g)
{
    uint32_t t = esp_cpu_get_cycle_count();
    gpio_set_level(PIN_DEPURACION, 1);
    banco_isr_t *b = &g_banco_isr[g];
    if (g_banco_t_ant[g] != 0) {
        uint32_t p = t - g_banco_t_ant[g];
        if (p < b->periodo_min || b->periodo_min == 0) b->periodo_min = p;
        if (p > b->periodo_max) b->periodo_max = p;
    }
    g_banco_t_ant[g] = t;
    return t;
}

static inline IRAM_ATTR void banco_isr_salida(int g, uint32_t t0)
{
    gpio_set_level(PIN_DEPURACION, 0);
    uint32_t d = esp_cpu_get_cycle_count() - t0;
    banco_isr_t *b = &g_banco_isr[g];
    b->llamadas++;
    b->ciclos_suma += d;
    if (d > b->ciclos_max) b->ciclos_max = d;
}

/* El MCPWM se detiene y arranca: la marca anterior se descarta para no
 * contar la pausa como periodo perdido. */
static inline void banco_isr_reiniciar(int g)
{
    g_banco_t_ant[g] = 0;
}

void banco_adc_muestra(int sensor);
void banco_adc_trama_isr(void);
void banco_disparo(void);
void banco_iniciar(void);

#else
static inline uint32_t banco_isr_entrada(int g) { (void)g; return 0; }
static inline void banco_isr_salida(int g, uint32_t t0) { (void)g; (void)t0; }
static inline void banco_isr_reiniciar(int g) { (void)g; }
static inline void banco_adc_muestra(int sensor) { (void)sensor; }
static inline void banco_adc_trama_isr(void) {}
static inline void banco_disparo(void) {}
static inline void banco_iniciar(void) {}
#endif
