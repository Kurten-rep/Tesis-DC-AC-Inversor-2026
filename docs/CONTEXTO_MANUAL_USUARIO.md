# Contexto para el manual de usuario

Este archivo reúne todo lo que debe quedar en el futuro manual de usuario del inversor
didáctico. No es el manual. Es la materia prima, ordenada por capítulo, para redactarlo después.

Reglas para mantenerlo:
- Cada vez que cambie algo que vea o haga el usuario (pantallas, secuencias, conexiones,
  mediciones, advertencias), se actualiza aquí en la misma sesión.
- Lo que todavía no se ha comprobado en el equipo va marcado con **[VERIFICAR]**.
- Las cifras salen del diseño vigente (`CONTEXTO_PROYECTO.md`) y del firmware (`firmware/main/config.h`).

Última actualización: 23 oct. 2026, firmware `1da8d2d`.

---

## 1. Qué es el equipo

Inversor DC-AC didáctico de dos canales para el laboratorio de conversión de energía.
Solo un canal funciona a la vez.

| | Canal 1 | Canal 2 |
|---|---|---|
| Tipo | Monofásico, puente H | Trifásico, puente de dos niveles |
| Entrada | 200 VDC, máximo 40 W (fuente externa) | 12 VDC a 24 VDC (PeakTech 6145) |
| Salida | 120 VRMS, 60 Hz, con filtro LC | Trifásica de 10 Hz a 1200 Hz, sin filtro |
| Modulación | SPWM unipolar, 20 kHz | SPWM con ley V/f en lazo abierto, 10 kHz o 20 kHz de base, hasta 25,2 kHz |
| Carga prevista | Carga resistiva o RL hasta 40 W | Motor A2212/13T 1000KV (14 polos), cargas RL |
| Corte por sobrecorriente | ±2 A | ±15 A por fase |

Toda la operación se hace desde la pantalla táctil de 2,8". No hay botones ni selectores físicos,
salvo el paro de emergencia.

## 2. Advertencias de seguridad

- El Canal 1 trabaja con 200 VDC y su salida es de 120 VRMS: los dos son peligrosos al contacto.
- Los negativos de las dos fuentes de potencia **nunca** se unen. Antes de usar un canal se
  desconecta físicamente la bornera del otro.
- El paro de emergencia es un pulsador de enclavamiento con contacto NC. Si se presiona o se
  corta su cable, las salidas se apagan por hardware, aunque el programa esté bloqueado.
- La protección por sobrecorriente es por software y el fusible de cada canal queda como respaldo.
- El equipo **no detecta** cortocircuito de rama (shoot-through). Solo lo previene el tiempo
  muerto de 800 ns (Canal 1) y 600 ns (Canal 2).
- Con el Canal 2 y el motor con hélice: el motor puede perder el sincronismo si se acelera muy
  rápido o se frena la hélice. Al pulsar DETENER el motor queda girando libre hasta detenerse.
- **[VERIFICAR]** El aislamiento del usuario se planteó con sensores ACS712 aislados, carcasa
  cerrada, bornes IEC 61010 y aislador USB o portátil a batería. Confirmar cuál se usa para la
  conexión USB.

## 3. Conexiones antes de encender

| Paso | Canal 1 | Canal 2 |
|---|---|---|
| 1 | Desconectar la bornera del Canal 2 | Desconectar la bornera del Canal 1 |
| 2 | Conectar la fuente de 200 VDC apagada | Conectar la fuente de 12 V a 24 V apagada |
| 3 | Conectar la carga a la salida del filtro | Conectar el motor o la carga RL a U, V, W |
| 4 | Fuente auxiliar de 15 V (drivers) | Fuente auxiliar de 15 V (drivers) |
| 5 | Verificar el paro liberado | Verificar el paro liberado |

**[VERIFICAR]** Orden recomendado de encendido y apagado de la fuente auxiliar y la de potencia.

## 4. La pantalla

Pantalla de 320 × 240 píxeles. Diseño de referencia en el lienzo "Interfaz TFT Inversor"
(claude.ai).

### 4.1 Barra de estado (arriba, en todas las pantallas)
- Izquierda: el estado actual (ARRANQUE, ESPERA, CONFIGURACIÓN, ARMADO, OPERACIÓN, FALLA,
  ERROR SENSOR), con color según el estado.
- Centro: el canal elegido o "Drivers apagados".
- Derecha: indicador del paro. Punto verde y "PARO OK" con el paro liberado. Punto rojo y
  "PARO ACTIVO" con el paro presionado.

### 4.2 Pantallas

| Pantalla | Qué muestra | Qué puede hacer el usuario |
|---|---|---|
| Arranque | "Calibrando sensores a 0 A…" con barra de progreso | Nada; dura unos 0,2 s |
| Error de sensor | Voltaje medido de cada sensor a 0 A, OK o FUERA | Reintentar |
| Espera | Dos tarjetas: Canal 1 y Canal 2 | Tocar un canal |
| Configuración Canal 1 | Parámetros fijos: 120 V, 60 Hz, ma 0,88, rampa de 1 s, 20 kHz, ±2 A | Volver o INICIAR |
| Configuración Canal 2 | Frecuencia de referencia (rpm y ma aproximados), botones −100, −10, +10, +100 Hz, conmutación base 10 kHz o 20 kHz | Ajustar, Volver o INICIAR |
| Armado | Verificaciones (canal inactivo apagado, paro liberado, 500 ms con compuertas apagadas) y barra de tiempo | Nada; dura 0,5 s |
| Operación Canal 1 | Corriente de salida RMS, f1, fsw, ma, barra de la rampa | DETENER |
| Operación Canal 2 | f1 actual y referencia, fsw, ma, rpm, corrientes RMS de U, V y W (W calculada) | −10, +10 Hz y DETENER |
| Falla | Causa, pico medido, umbral, canal y frecuencia al disparo | RECONOCER FALLA (inactivo mientras el paro esté presionado) |

Los botones −10 y +10 repiten el paso si se mantienen presionados.

Los números se muestran con coma decimal.

### 4.3 Avisos que pueden aparecer
- "Libere el paro de emergencia": se pulsó INICIAR con el paro presionado.
- "El paro sigue presionado": se pulsó RECONOCER con el paro presionado.
- "Conecte el bus de 200 V y desconecte el bus del Canal 2" (Configuración Canal 1).
- "Conecte el bus de 12 V a 24 V y desconecte el del Canal 1" (Configuración Canal 2).
- "Paro presionado: libérelo para operar" (Espera).

## 5. Secuencia de operación

1. Encender: el equipo calibra los sensores con los drivers apagados.
   - Si algún sensor da un voltaje fuera de 1,57 V a 1,77 V, aparece ERROR SENSOR y no se puede operar.
2. ESPERA: tocar el canal.
3. CONFIGURACIÓN: en el Canal 2, elegir la frecuencia (10 Hz a 1200 Hz) y la conmutación base.
   Para arrancar el motor se recomienda la base de 10 kHz.
4. INICIAR: el equipo pasa a ARMADO, verifica las condiciones y espera 500 ms.
5. OPERACIÓN:
   - Canal 1: el índice de modulación sube de 0 a 0,88 en 1 s, para limitar la corriente de arranque del filtro.
   - Canal 2: la frecuencia sube en rampa desde 10 Hz hasta la referencia, a 100 Hz/s **[VERIFICAR, valor inicial]**.
6. DETENER: apaga el canal de inmediato y vuelve a ESPERA.

### 5.1 Qué pasa en una falla
- Sobrecorriente o paro: las salidas se apagan, la pantalla pasa a FALLA y muestra la causa.
- Para salir: revisar la carga, liberar el paro y pulsar RECONOCER FALLA. El equipo vuelve a ESPERA.
- "Error interno de habilitación": el equipo detectó habilitado el canal que debía estar
  apagado durante el armado. Revisar la tarjeta de control antes de volver a operar.

### 5.2 Relación entre frecuencia y velocidad del motor (Canal 2)
Velocidad en rpm = f1 · 60 / 7 (7 pares de polos). Ejemplo: 600 Hz ≈ 5143 rpm.
La conmutación sigue la regla fsw = máx(fbase, 21 · f1), con tope en 25,2 kHz.

## 6. Mediciones con el osciloscopio

Instrumento de referencia del laboratorio: Hantek DSO2C10 (2 canales, 100 MHz, 1 GS/s, disparo
por patrón, matemática con suma y resta y FFT, 300 V CAT II por entrada, según la ficha de TME).
El plan de pruebas no se ata a este modelo.

Para no confundir los canales del inversor con los del osciloscopio, en el manual se
dirá **sonda 1** y **sonda 2**.

### 6.1 Regla de tierra (la más importante)
Las tierras de las sondas están unidas entre sí y a la tierra del enchufe **[VERIFICAR en el
manual del osciloscopio]**. Por eso:
1. Las dos pinzas de tierra van solo al negativo del bus del canal activo, que es también la GND lógica.
2. Nunca en el punto medio de una rama, un borne de la carga ni una fase del motor.
3. Nunca en el negativo del canal inactivo, porque uniría los dos negativos.

Use siempre la sonda en ×10 y con el resorte de tierra corto. En el Canal 1 revise que la
etiqueta de la sonda admita el voltaje en ×10.

### 6.2 Canal 2
| Medición | Punta | Escala | Esperado |
|---|---|---|---|
| Voltaje de fase | U, V o W contra el negativo | 5 V/div | PWM de 0 al voltaje del bus |
| Voltaje de línea | Sonda 1 en U, sonda 2 en V, resta | 5 V/div | PWM de ±voltaje del bus |
| Fundamental de línea | Igual, con filtro RC de dos etapas | 1 V/div | 7,35 VRMS a 1200 Hz con bus de 12 V y ma = 1, algo menos por el límite de 95 % |
| Corriente de fase | Salida del ACS712 contra GND lógica | 200 mV/div | 2,5 V + 100 mV/A |

### 6.3 Canal 1
- Forma de onda: sonda 1 en la salida de la rama A (después del inductor), sonda 2 en la rama B,
  las dos contra el negativo del bus, y resta. 50 V/div con sonda ×10. Unos 170 V pico a 60 Hz.
- Limitación: las dos ramas conmutan 0 a 200 V a 20 kHz. Si las sondas no son idénticas, la resta
  deja picos residuales que la FFT muestra como armónicos que no existen. Para el THD conviene
  una sonda diferencial de alto voltaje. **[PENDIENTE] Definir cómo se mide el THD del Canal 1.**

## 7. Prueba de banco (sin drivers)

Para el técnico, antes de conectar las tarjetas de disparo. Procedimiento completo en
`firmware/PRUEBA_BANCO.md`.

- Se compila con `idf.py -B build_banco -DMODO_BANCO=1 build flash monitor`. Cada 2 s la consola
  muestra el tiempo de la ISR, la carga de los núcleos, las muestras/s del ADC y el tiempo de
  reacción de la protección, con veredicto OK o NO CUMPLE.
- Los sensores se reemplazan por trimmers ajustados a 1,67 V. La sobrecorriente se simula con
  un pulsador y una resistencia a 3,3 V: 4,7 kΩ en GPIO 1 para el Canal 1 y 1 kΩ en GPIO 2 para el Canal 2.
- GPIO 38 marca la duración de la ISR en el osciloscopio **[VERIFICAR que no sea el pin del LED RGB]**.

### 7.1 Señales HIN y LIN esperadas (pines del ESP32-S3)
| Pines | Canal 1: GPIO 15/16 (rama A), 17/18 (rama B) | Canal 2: 39/40 (U), 41/42 (V), 47/21 (W) |
|---|---|---|
| Frecuencia | 20,00 kHz | 10 kHz a 25,2 kHz según f1 |
| Tiempo muerto | 800 ns en los dos flancos | 600 ns |
| HIN y LIN en alto a la vez | Nunca (verificar con disparo por patrón) | Nunca |
| Ciclo de trabajo de HIN | ≈ 4,4 % a 92,4 % | Hasta 95 % menos el tiempo muerto |
| Freno (paro, sobrecorriente, DETENER) | HIN y LIN en 0 V | HIN y LIN en 0 V |

- El tiempo muerto se mide con cursores entre los puntos del 50 % (1,65 V).
- A 1200 Hz en el Canal 2, el pulso de LIN nunca debe desaparecer, porque recarga el bootstrap.
- Con un filtro RC de 10 kΩ y 100 nF en HIN_A se ve la senoide de 60 Hz, de unos 1,36 V pico
  centrada en 1,62 V.

## 8. Mantenimiento y programación

- Entorno: VS Code con la extensión ESP-IDF de Espressif, ESP-IDF v5.3.x. No se usa Arduino,
  porque no permite activar las opciones que dejan la ISR de la SPWM en IRAM.
- Compilar y cargar: `idf.py build`, luego `idf.py -p COMx flash monitor`, en la carpeta `firmware/`.
- Parámetros ajustables en `firmware/main/config.h`: rampa V/f, incremento a baja frecuencia,
  umbrales, sensibilidad de los sensores, orientación de la pantalla y calibración del táctil.

### 8.1 Calibración del táctil
El panel es resistivo y lo lee un XPT2046 por el mismo bus SPI de la pantalla. Para calibrarlo:
1. Poner `TACTIL_DEPURAR 1` en `config.h`, compilar y cargar.
2. Tocar las cuatro esquinas y anotar las lecturas crudas que salen en el monitor serie.
3. Copiar los valores mínimos y máximos en `TACTIL_X_MIN`, `TACTIL_X_MAX`, `TACTIL_Y_MIN` y `TACTIL_Y_MAX`.
4. Si un eje sale al revés o cruzado, cambiar `TACTIL_INVERTIR_X`, `TACTIL_INVERTIR_Y` o `TACTIL_INTERCAMBIAR_XY`.

Un toque se acepta cuando la presión medida supera `TACTIL_Z_UMBRAL`.

## 9. Pendientes que afectan el manual

- [ ] Polaridad de EN confirmada contra el esquemático.
- [ ] Orientación de la pantalla y calibración del táctil en el módulo real.
- [ ] Pendiente de la rampa V/f e incremento ma0 ajustados con el motor.
- [ ] Sensor del Canal 1: ACS712-20A o 05B.
- [ ] Método de medición del THD del Canal 1.
- [ ] Orden de encendido de las fuentes.
- [ ] Fotos del equipo terminado y capturas de cada pantalla.
- [ ] Tiempo de reacción medido de la protección por software.
