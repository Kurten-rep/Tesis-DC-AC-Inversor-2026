# Prueba de banco del MCU (sin drivers)

Esta prueba verifica el ESP32-S3 y el firmware antes de conectar las tarjetas de disparo.
No se conecta ningún IR2110, ningún bus de potencia ni los sensores ACS712. Todo se mide
en los pines del ESP32-S3 con osciloscopio y multímetro.

## 1. Materiales

| Elemento | Valor | Tolerancia y coeficiente |
|---|---|---|
| Tarjeta ESP32-S3-N16R8 y pantalla TFT de 2,8" | | |
| 3 trimmers (simulan los sensores a 0 A) | 10 kΩ | ±10 %, ±100 ppm/°C (cermet) |
| Pulsador NC (paro) y resistencia de pull-up | 10 kΩ | ±1 %, ±100 ppm/°C |
| Resistencias para el escalón de corriente | 4,7 kΩ y 1 kΩ | ±5 %, ±200 ppm/°C |
| Pulsador NA para el escalón | | |
| Filtro RC del Canal 1 | 10 kΩ y 100 nF | ±1 %, ±100 ppm/°C; película, ±5 % |
| Filtro RC del Canal 2 (dos etapas por fase) | 10 kΩ + 10 nF, luego 100 kΩ + 1 nF | ±1 %, ±100 ppm/°C; C0G, ±5 % |
| Osciloscopio de 2 canales o más, multímetro | | |

Ningún pin del ESP32-S3 debe recibir más de 3,3 V.

## 2. Montaje

| Pin | Conexión |
|---|---|
| GPIO 1, 2, 4 (I_CH1, I_U, I_V) | Cursor de un trimmer entre 3,3 V y GND, ajustado a 1,67 V |
| GPIO 7 (paro) | Pulsador NC a GND y 10 kΩ a 3,3 V |
| GPIO 5, 6 (EN) | Sin conectar, se miden con el multímetro |
| GPIO 15 a 18, 39 a 42, 47, 21 | Sin conectar, se miden con el osciloscopio |
| GPIO 38 (depuración) | Osciloscopio. Queda en alto mientras corre la ISR |
| SPI2, GPIO 9 a 14 | Pantalla y táctil, como en la tarjeta final |

Escalón de corriente: un pulsador NA en serie con la resistencia, entre 3,3 V y el pin del sensor.
Con el trimmer a la mitad, su resistencia de Thévenin es 2,5 kΩ:

$$V_{pin} = \frac{1{,}67\ \text{V} \cdot R + 3{,}3\ \text{V} \cdot 2{,}5\ \text{k}\Omega}{R + 2{,}5\ \text{k}\Omega}$$

- Con R = 4,7 kΩ en GPIO 1 el pin sube a 2,24 V. Supera el umbral del Canal 1, que queda en 1,67 V + 2 A · 66,7 mV/A = 1,80 V.
- Con R = 1 kΩ en GPIO 2 el pin sube a 2,83 V. Supera el umbral del Canal 2, que queda en 1,67 V + 15 A · 66,7 mV/A = 2,67 V.

## 3. Compilar el modo de banco

```
cd firmware
idf.py -B build_banco -DMODO_BANCO=1 build flash monitor
```

La carpeta `build_banco` separa esta versión de la normal (`idf.py build`), que no incluye nada
del banco. Cada 2 s el monitor imprime un reporte con veredictos `OK` o `NO CUMPLE`.

## 4. Pruebas

### P1. Pines seguros al arrancar
Mida EN_CH1 y EN_CH2 con el multímetro mientras pulsa reset y durante el arranque.
- **Criterio:** los dos quedan en nivel inactivo (0 V con `EN_NIVEL_ACTIVO = 1`) en todo momento.

En el osciloscopio, HIN y LIN deben quedar en bajo después del arranque.

### P2. Calibración de los sensores
Con los tres trimmers en 1,67 V el equipo pasa a ESPERA. Luego baje un trimmer a 1,40 V, pulse
reset y verifique que aparece ERROR SENSOR con ese sensor marcado. Vuelva a 1,67 V y pulse Reintentar.
- **Criterio:** se bloquea con el offset fuera de 1,57 V a 1,77 V y se libera al corregirlo.

### P3. Señales del Canal 1
Inicie el Canal 1 y mida HIN_A (GPIO 15) y LIN_A (GPIO 16) al mismo tiempo.

| Medición | Esperado |
|---|---|
| Frecuencia | 20,0 kHz (periodo de 50 µs) |
| Tiempo muerto, HIN baja a LIN sube y LIN baja a HIN sube | 800 ns |
| HIN y LIN en alto a la vez | Nunca |
| Ciclo de trabajo máximo de HIN | ≤ 95 % |

Filtro RC en HIN_A con $R = 10\ \text{k}\Omega$ y $C = 100\ \text{nF}$:

$$f_c = \frac{1}{2\pi R C} = \frac{1}{2\pi \cdot 10\,000 \cdot 100 \times 10^{-9}} \approx 159{,}2\ \text{Hz}$$

$$|H(60\ \text{Hz})| = \frac{1}{\sqrt{1 + (60/159{,}2)^2}} \approx 0{,}936$$

Después de la rampa de 1 s se ve una senoide de 60 Hz. Su amplitud es
$0{,}5 \cdot 0{,}88 \cdot 3{,}3\ \text{V} \cdot 0{,}936 \approx 1{,}36\ \text{V}$ pico.
Queda centrada cerca de $1{,}65\ \text{V} \cdot (1 - 800\ \text{ns} \cdot 20\ \text{kHz}) \approx 1{,}62\ \text{V}$,
porque el tiempo muerto acorta el pulso de HIN, y tiene un rizado de unos 41 mV pico a pico.
HIN_B (GPIO 17) da la misma onda en contrafase.

### P4. Señales del Canal 2
Configure 600 Hz con base de 10 kHz e inicie. Ponga un filtro de dos etapas en HIN_U (GPIO 39)
y en HIN_V (GPIO 41).

| Medición | Esperado |
|---|---|
| Frecuencia de HIN_U | 12,6 kHz (modulación síncrona, 21 · 600 Hz) |
| Tiempo muerto entre HIN_U y LIN_U | 600 ns |
| Fundamental filtrada | 600 Hz, unos 0,76 V pico, con atraso de unos 41° |
| Desfase entre U y V filtradas | 120° (el atraso del filtro es igual en las dos) |

Repita a 300 Hz, donde fsw debe ser 10 kHz, y a 1200 Hz, donde debe ser 25,2 kHz.

### P5. Rendimiento de la ISR
Haga esta prueba en el peor caso: Canal 2 a 1200 Hz (fsw = 25,2 kHz, periodo de 39,7 µs). Espere a
que la rampa termine y lea el reporte.
- **Criterio 1:** tiempo máximo de la ISR ≤ 25 % del periodo, es decir ≤ 9,9 µs.
- **Criterio 2:** el periodo máximo entre llamadas es menor que 1,5 periodos, sin interrupciones perdidas.
- **Criterio 3:** llamadas por segundo dentro de ±2 % de fsw.

Mientras tanto, toque la pantalla y cambie de pantalla varias veces para cargar el núcleo 0. Los
valores no deben cambiar. En GPIO 38, el ancho del pulso es la duración de la ISR. Con persistencia
en el osciloscopio se ve su variación.

### P6. ADC
- **Criterio:** cada sensor entre 19 000 y 21 000 muestras/s y 0 tramas perdidas.

### P7. Protección por sobrecorriente
Canal 1 en operación. Ponga el canal 1 del osciloscopio en GPIO 1, disparando con el flanco de
subida, y el canal 2 en HIN_A. Pulse el escalón de 4,7 kΩ.
- **Criterio:** HIN_A y LIN_A quedan en bajo menos de 2 ms después del escalón. EN_CH1 pasa a inactivo
  y la pantalla muestra FALLA con "Sobrecorriente en el Canal 1".

El reporte muestra el tiempo desde que llega la trama del ADC hasta el freno. Repita en el Canal 2
con el escalón de 1 kΩ en GPIO 2.

### P8. Paro de emergencia
Con un canal en operación, presione el paro.
- **Criterio:** todas las salidas PWM quedan en bajo, EN pasa a inactivo, aparece FALLA y
  "Reconocer" queda inactivo mientras el paro siga presionado.

### P9. Exclusión entre canales
Opere el Canal 1, deténgalo e inicie el Canal 2. Mida EN_CH1 y EN_CH2 en el osciloscopio.
- **Criterio:** nunca están activos a la vez, y entre el apagado de uno y el encendido del otro pasan al menos 500 ms.

### P10. Carga y memoria (informativo)
Anote la carga de cada núcleo y la memoria libre del reporte. Son la referencia para comparar si
el firmware crece.

## 5. Registro

| Prueba | Resultado medido | Cumple |
|---|---|---|
| P1 Pines seguros | | |
| P2 Calibración | | |
| P3 Canal 1: fsw / tiempo muerto / amplitud | | |
| P4 Canal 2: fsw / tiempo muerto / desfase | | |
| P5 ISR: máximo / periodo máximo / tasa | | |
| P6 ADC: muestras/s / tramas perdidas | | |
| P7 Reacción CH1 / CH2 | | |
| P8 Paro | | |
| P9 Exclusión | | |
| P10 Carga núcleo 0 / núcleo 1 / memoria | | |
