# Registro de cambios

Cambios de diseño y de documentación, del más reciente al más antiguo. Cada herramienta
(Claude, Antigravity o el equipo) agrega aquí una entrada cuando cambia una decisión.
Formato de cada entrada: fecha, quién, qué cambió y qué reemplaza.

---

## 2026-10-19 · Claude

- Se creó el repositorio con la estructura `hardware/`, `firmware/`, `simulaciones/`,
  `documento/`, `datasheets/`, `referencias/` y `docs/`.
- `CONTEXTO_PROYECTO.md` consolida el estado vigente. El contexto anterior (basado en la Pico)
  quedó en `docs/historico/CONTEXTO_PROYECTO_TESIS.md` solo como referencia.
- Se agregó `AGENTS.md` con las reglas de trabajo compartidas entre Claude y Antigravity.
- Se subieron los `.docx` de avances y del Capítulo 3, las figuras 1 a 30, los scripts de MATLAB y Python de los anexos, los planos esquemáticos A3 y la tabla de huellas de EasyEDA.

---

## 2026-10-18 · Claude

Complementa la entrada del 17 de octubre. Donde haya conflicto, manda esta.

- **Protección por software (reemplaza los comparadores).** Se retiraron los comparadores de
  sobrecorriente. El ESP32-S3 compara cada muestra del ADC1 con el umbral del canal activo
  (±2 A en CH1, ±15 A en fases U y V de CH2) y, si lo supera, deshabilita los drivers (EN) y
  detiene el MCPWM. El fusible de cada canal queda como respaldo. Pendiente: medir el tiempo de reacción.
- FALLA_N ya no lleva comparador. GPIO 7 queda libre (opción: paro de emergencia físico).
- Tabla 9 del documento ajustada a la asignación de pines del 17 de octubre
  (MCPWM0: 15 a 18; MCPWM1: 39, 40, 41, 42, 47, 21; EN: 5 y 6; SPI2: 12, 11, 13, 10, 9; táctil: 14 y 8).
- Resistencias de descarga de 10 kΩ en HO-VS y en LO-COM de cada IR2110.
- Simulación del Canal 1 con tiempo muerto de 800 ns: 114.6 VRMS y THD 2.4 %
  (ideal: 120.3 VRMS y 0.12 %). Opciones: ma ≈ 0.89 o compensación del tiempo muerto en firmware.
- Las figuras 9, 21, 23 y 25 se generan en MATLAB (scripts en la carpeta "Figuras Tesis").
- La Figura 18 usa el diagrama de bloques propio del equipo, que todavía muestra el elevador
  DC-DC de 64 V a 200 V que se descartó.

---

## 2026-10-17 · Claude

Reemplaza los valores de `docs/historico/CONTEXTO_PROYECTO_TESIS.md`, del anteproyecto y del
Documento de Proyecto de Grado donde haya conflicto.

### Componentes definitivos
- IRFP360 (CH1), IRLZ44A (CH2), IR2110 (drivers). STM32F103, FDH45N50F, IXFH60N50P3 e IR21844
  se usan solo para la matriz de selección.
- **Microcontrolador: ESP32-S3-N16R8, reemplaza a la Raspberry Pi Pico.** Razones: placa ya
  disponible, el equipo lo programa con más facilidad, la PCB de control aún no estaba diseñada,
  y el MCPWM trae tiempo muerto y entrada de fallo por hardware, además de FPU y RAM para la
  interfaz táctil. El conteo de pines no fue decisivo (Pico 26 útiles, ESP32-S3 en placa unos 24).

### Canal 1
- Fuente de 200 VDC, 40 W máximo. Bus exclusivo de CH1. Salida 120 VRMS, 60 Hz, ma ≈ 0.85.
- Diseñado para 640 VA, uso limitado a 40 W por la fuente. Sin etapa boost (fuera de alcance).
  Un boost desde 64 V / 10 A necesitaría ≈ 732 W de entrada para entregar 640 VA.
- Objetivo de THD: 10 % (no el 8 % de IEEE 519).

### Canal 2
- Fuente PeakTech 6145 con conectores propios: 12 V para el motor, hasta 24 V para cargas RL.
  Reemplaza el bus fijo de 24 V.
- Motor A2212/13T 1000KV, 14 polos. 7.2 a 12 V, 0.5 A en vacío, 13 A/60 s, 150 W.
- SPWM con ley V/f en lazo abierto. Reemplaza la opción SVPWM.
- Fundamental ~10 Hz a ~1.2 kHz (reemplaza 10 a 400 Hz). Conmutación seleccionable 10 a 20 kHz.

### Canales y tierras
- Canales mutuamente excluyentes. Los negativos de CH1 y CH2 nunca se unen; el canal inactivo
  se desconecta físicamente, abriendo su negativo.

### Alimentación secundaria
- Fuente de 15 V para drivers. Regulador de 5 V en la PCB de control.

### Aislamiento
- Se mantiene IR2110 (no aislado). El requisito se reformula como aislamiento del usuario
  respecto a la potencia.

### Interfaz
- Toda en la TFT ILI9341 2.8" con táctil XPT2046. Reemplaza pulsadores y OLED.

### Sensado
- Solo corrientes: un sensor en CH1 y fases U y V en CH2 (W = −(iU + iV)). Reemplaza los
  4 sensores de la versión con Pico.
- Divisor 10 kΩ / 20 kΩ. Reemplaza 10 kΩ / 18 kΩ.
- No se miden las tensiones de los buses DC.

### Asignación de pines
- Ver `CONTEXTO_PROYECTO.md`, sección 6.

### Documento de grado
- Estructura de 8 capítulos + anexos y estilo de redacción aprobados (muestra: sección 2.5.2).
