# Firmware del ESP32-S3-N16R8

Firmware en C con ESP-IDF 5.3 y LVGL 9.2. Implementa la máquina de estados,
la modulación y la protección descritas en la sección 4.3.8 del documento
(Figuras 27 a 30). Las pantallas siguen el lienzo "Interfaz TFT Inversor".

## Compilar y cargar

```
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

La primera compilación descarga LVGL del registro de Espressif
(`main/idf_component.yml`). El driver del ILI9341 va copiado en
`components/esp_lcd_ili9341` (Apache-2.0).

## Prueba de banco (MCU sin drivers)

```
idf.py -B build_banco -DMODO_BANCO=1 build flash monitor
```

Procedimiento, montaje y criterios en [PRUEBA_BANCO.md](PRUEBA_BANCO.md).

## Pruebas en el PC

```
make -C firmware/test
```

`test_estado` recorre la máquina de estados con hardware simulado y comprueba que
nunca se habilitan los dos canales a la vez. `test_spwm` simula los comparadores
periodo a periodo y calcula la fundamental con una DFT de un número entero de
periodos (sin ventana ni relleno de ceros, espectro de un solo lado). Resultados
con la versión actual:

| Caso | Fundamental | Esperado |
|---|---|---|
| CH1, ma = 0,88, 200 V | 124,4 VRMS (≈ 118,7 VRMS con la caída por tiempo muerto) | 124,5 VRMS ideal, 118,9 VRMS en la simulación del documento |
| CH2, 300 Hz, 12 V | 2,99 V pico de línea | 2,99 V |
| CH2, 600 Hz, 12 V | 5,46 V pico de línea | 5,46 V |
| CH2, 1200 Hz, 12 V | 10,19 V pico de línea | 10,39 V (el límite de 95 % del bootstrap recorta 2 %) |

## Archivos

| Archivo | Contenido |
|---|---|
| `main/pines.h` | Asignación de pines (Tabla 15) |
| `main/config.h` | Parámetros, con los valores por ajustar marcados |
| `main/estado.c` | Máquina de estados, sin acceso al hardware |
| `main/potencia.c` | MCPWM, tiempo muerto, freno por falla, ISR de la SPWM, rampa V/f |
| `main/spwm.c` | Tabla de seno, ciclos de trabajo, ley V/f, ecuación 23 |
| `main/medicion.c` | ADC1 continuo, calibración de offset, protección, valor eficaz |
| `main/pantalla.c` | SPI2, ILI9341, XPT2046 y LVGL |
| `main/ui.c` | Pantallas de cada estado |
| `main/main.c` | Arranque y tarea de interfaz de 50 ms |
| `main/banco.c` | Modo de banco: tiempos de la ISR, carga, ADC, reacción de la protección |

## Por verificar en el hardware

Están marcados con `[VERIFICAR]` o `[PENDIENTE]` en `config.h`.

- Polaridad de EN (`EN_NIVEL_ACTIVO`): se supuso que EN en alto lleva SD a nivel bajo.
- Orientación de la pantalla y calibración del táctil. Con `TACTIL_DEPURAR 1` se
  imprimen las lecturas crudas del XPT2046 para ajustar los límites.
- Sensibilidad del sensor de CH1 (ACS712-20A o 05B, pendiente en el contexto).
- Incremento `CH2_MA0` y pendiente `CH2_PENDIENTE_HZ_S` de la rampa V/f.
- Tiempo de reacción de la protección por software (se espera entre 1 ms y 2 ms).

## Decisiones del firmware

- El freno del MCPWM actúa al final de la cadena de salida (después del tiempo
  muerto) y lleva HIN y LIN a bajo. Se usa para el paro de emergencia (GPIO 7,
  por hardware) y para la sobrecorriente y el apagado normal (falla por software,
  una por rama, porque ESP-IDF no deja compartirla entre operadores).
- DETENER apaga el canal de inmediato; el motor del Canal 2 queda girando libre.
- Durante ARMADO se exige todo el tiempo que el canal inactivo siga con SD en alto
  y que el paro esté liberado. Si EN del canal inactivo aparece activo, el equipo
  pasa a FALLA con "error interno de habilitación".
- La ISR no usa coma flotante (el ESP32-S3 no la admite en interrupciones); la
  tarea de rampa calcula las referencias y las publica con doble buffer.
