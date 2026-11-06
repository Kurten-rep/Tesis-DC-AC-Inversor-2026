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
| CH1, ma = 0,88, 200 V, 10 kHz | 124,4 VRMS (≈ 121,6 VRMS con la caída por tiempo muerto) | 124,5 VRMS ideal. Los 118,9 VRMS de la simulación del documento son con 20 kHz |
| CH2, 300 Hz, 12 V | 2,99 V pico de línea | 2,99 V |
| CH2, 600 Hz, 12 V | 5,46 V pico de línea | 5,46 V |
| CH2, 1200 Hz, 12 V | 10,19 V pico de línea | 10,39 V (el límite de 95 % del bootstrap recorta 2 %) |

## Estructura del proyecto

```
firmware/
├── CMakeLists.txt            Proyecto de ESP-IDF
├── sdkconfig.defaults        Configuración base de la placa, FreeRTOS, MCPWM y LVGL
├── README.md                 Este archivo
├── PRUEBA_BANCO.md           Procedimiento de la prueba del MCU sin drivers
├── .gitignore  .clangd       Archivos ignorados por git y configuración del editor
├── main/                     Código del equipo (componente "main")
├── components/               Componentes copiados al proyecto
│   └── esp_lcd_ili9341/      Driver del ILI9341 (Apache-2.0)
├── test/                     Pruebas en el PC
├── build/ build_banco/       Salida de la compilación (se genera, no se edita)
└── managed_components/       LVGL descargado por idf.py (se genera, no se edita)
```

## Archivos de la raíz

| Archivo | Contenido |
|---|---|
| `CMakeLists.txt` | Define el proyecto `inversor_dc_ac`. Si existe la variable de entorno `FW_COMPONENTES_EXTRA`, la agrega como carpeta de componentes (sirve para una copia local de LVGL sin acceso a internet). |
| `sdkconfig.defaults` | Valores de configuración que se aplican al crear el `sdkconfig`: ESP32-S3 con 16 MB de flash y 8 MB de PSRAM octal, CPU a 240 MHz, FreeRTOS a 1 kHz, consola por el USB nativo, ISR del MCPWM y del ADC en IRAM, estadísticas de tiempo de ejecución para el modo de banco, LVGL con color de 16 bits y 96 kB de memoria, y optimización para rendimiento. |
| `sdkconfig`, `sdkconfig.old` | Configuración resultante, la genera `idf.py`. No se edita a mano; los cambios van en `sdkconfig.defaults`. |
| `PRUEBA_BANCO.md` | Materiales, montaje, pasos y criterios de aceptación de la prueba de banco. |
| `dependencies.lock` | Versiones exactas de los componentes descargados, la genera `idf.py`. |

## Carpeta `main/`: código del equipo

### Configuración

| Archivo | Contenido |
|---|---|
| `pines.h` | Asignación de cada GPIO: sensores de corriente (ADC1), habilitación EN de los drivers, paro de emergencia (GPIO 7), bus SPI de la pantalla y el táctil, señales HIN y LIN de los dos canales y el pin de depuración. Es el único archivo que se cambia si se mueve una conexión. |
| `config.h` | Parámetros del firmware con el origen de cada valor: tiempos del MCPWM y tiempos muertos, frecuencias y umbrales de cada canal, sensado y rango válido del offset, ADC, pantalla (orientación, color y perfil), calibración del táctil, modo de banco y polaridad de EN. Los valores por ajustar llevan `[PENDIENTE]` o `[VERIFICAR]`. |
| `idf_component.yml` | Dependencias que descarga `idf.py`: LVGL 9.2 y ESP-IDF 5.3 o superior. |
| `CMakeLists.txt` | Lista los archivos fuente y los componentes de ESP-IDF que usa `main`. Con `-DMODO_BANCO=1` activa el modo de banco. |

### Lógica y control

| Archivo | Contenido |
|---|---|
| `estado.h` / `estado.c` | Máquina de estados (Figura 27). Define los estados (ARRANQUE, ERROR SENSOR, ESPERA, CONFIGURACIÓN, ARMADO, OPERACIÓN, FALLA), los eventos, las causas de falla y la tabla de funciones de hardware `sm_hw_t`. No toca el hardware, por eso se prueba en el PC. Incluye la espera de 500 ms de ARMADO, las verificaciones del canal inactivo y del paro, y el modo de pruebas con `sensores_ok`. |
| `spwm.h` / `spwm.c` | Matemática de la modulación sin dependencias del hardware: tabla de seno en Q15, valor de comparación del temporizador, ley V/f, regla de la frecuencia de conmutación (ecuación 23), rampas y conversiones. La ISR solo usa las funciones de enteros; las de coma flotante las usa la tarea de rampa. |
| `potencia.h` / `potencia.c` | Todo lo que mueve los pines de potencia: configuración de los dos grupos MCPWM (tiempo muerto, freno por falla, paro por GPIO 7), líneas EN, ISR de la SPWM de cada canal, tarea de rampa de 1 ms con doble buffer de referencias, y los comandos arrancar, habilitar, disparo y apagado total. |
| `medicion.h` / `medicion.c` | ADC1 continuo por DMA a 60 kS/s (tres sensores), tabla de conversión a mV, calibración del offset, protección por sobrecorriente, cálculo de la corriente de la fase W como -(U + V) y valor eficaz sobre un número entero de periodos. |
| `main.c` | `app_main()` con el orden de arranque (drivers apagados primero), cola de eventos, funciones de hardware de la máquina de estados y la tarea de interfaz de 50 ms. Reparto de núcleos: el 1 para la ISR y la rampa, el 0 para la protección y LVGL. |

### Pantalla e interfaz

| Archivo | Contenido |
|---|---|
| `pantalla.h` / `pantalla.c` | Bus SPI2 compartido, ILI9341 con su secuencia de registros y los perfiles de color (`LCD_PERFIL`), táctil XPT2046 con su calibración, buffers de dibujo y la tarea de LVGL. |
| `ui.h` / `ui.c` | Las nueve pantallas (arranque, error de sensor, espera, configuración de cada canal, armado, operación de cada canal y falla), la barra de estado superior y la paleta de colores. Los botones solo envían eventos a la máquina de estados. |
| `fuentes/fuente_10.c`, `fuente_12.c`, `fuente_14.c`, `fuente_16.c`, `fuente_28.c` | Montserrat SemiBold en cinco tamaños, con los símbolos de FontAwesome. Generadas con `lv_font_conv`. |
| `fuentes/fuente_negrita_10.c`, `_14.c`, `_16.c`, `_20.c` | Montserrat Bold para los títulos de canal. Generadas con `lv_font_conv`. |

Las fuentes son archivos generados: no se editan a mano. Para cambiarlas se vuelve a
correr `lv_font_conv`.

### Diagnóstico

| Archivo | Contenido |
|---|---|
| `banco.h` / `banco.c` | Modo de banco (`MODO_BANCO=1`). Mide el tiempo de la ISR, el periodo entre interrupciones, la carga de cada núcleo, las muestras por segundo del ADC y el tiempo de reacción de la protección, e imprime un veredicto cada 2 s. Con `MODO_BANCO=0` las funciones quedan vacías y no agregan código. |

## Carpeta `components/`

| Archivo | Contenido |
|---|---|
| `esp_lcd_ili9341/` | Copia local del driver del ILI9341 de Espressif, con su `license.txt` y su README. `pantalla.c` le pasa la secuencia de inicio propia por `vendor_config`. |

## Carpeta `test/`

| Archivo | Contenido |
|---|---|
| `Makefile` | Compila y ejecuta las dos pruebas con `make -C firmware/test`. `make clean` borra los ejecutables. |
| `test_estado.c` | Recorre la máquina de estados con hardware simulado y comprueba que nunca se habilitan los dos canales a la vez. |
| `test_spwm.c` | Simula los comparadores periodo a periodo y calcula la fundamental con una DFT. Los resultados están en la tabla de la sección anterior. |

## Interruptores de `config.h` que conviene conocer

| Constante | Efecto |
|---|---|
| `SENSORES_BLOQUEAN_INTERFAZ` | 1: una calibración fallida bloquea la interfaz en ERROR SENSOR (comportamiento final). 0: la interfaz sigue y los drivers nunca se habilitan (pruebas del MCU). Volver a 1 antes de conectar la etapa de potencia. |
| `MODO_BANCO` | 1: compila el reporte por consola de `banco.c`. Se activa desde la línea de comandos. |
| `LCD_PERFIL` | Perfil de potencia, VCOM y gamma de la pantalla (1, 2, 3, 5 o 6). Cambia la saturación aparente de los colores. |
| `LCD_INTERCAMBIAR_XY`, `LCD_ESPEJO_X`, `LCD_ESPEJO_Y`, `LCD_ORDEN_BGR` | Orientación y orden de color de la pantalla. |
| `TACTIL_*` | Calibración e inversión de ejes del táctil. Con `TACTIL_DEPURAR 1` se imprimen las lecturas crudas. |
| `EN_NIVEL_ACTIVO` | Nivel lógico de EN que habilita un canal. |

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
