# Contexto del proyecto: Inversor DC-AC didáctico de doble canal

Universidad El Bosque, Facultad de Ingeniería, Programa de Ingeniería Electrónica.

Este archivo describe el **estado vigente** del diseño. Cuando haya conflicto con
`docs/historico/` (versiones anteriores, basadas en la Raspberry Pi Pico),
manda este archivo. El detalle de cada cambio y su fecha está en `CAMBIOS.md`.

## 1. Identificación

- **Título:** Diseño y desarrollo de un inversor DC-AC didáctico para análisis de comportamiento
  y características funcionales en prácticas del laboratorio de conversión de energía.
- **Autores:** Camilo Andrés Herrera Hoyos y Mariapaula Marentes Niño.
- **Director:** Ing. Fredy Rolando García Bello.
- **Plazo:** terminar lo antes posible, sin cronograma fijo.

## 2. Arquitectura general

```
            Fuente 15 V (drivers)   Regulador 5 V (en PCB de control)
                         |
              +----------v-----------+
              |  ESP32-S3-N16R8      |  TFT ILI9341 2.8" + táctil XPT2046 (SPI2)
              |  MCPWM0 -> CH1       |  ADC1: I_CH1, I_U, I_V
              |  MCPWM1 -> CH2       |  EN_CH1, EN_CH2
              +----+------------+----+
                   |            |
          2x IR2110 (CH1)   3x IR2110 (CH2)
                   |            |
     Puente H 4x IRFP360    Puente 3F 6x IRLZ44A
     Bus 200 VDC / 40 W     Bus propio 12 a 24 VDC
     Filtro LC              Motor A2212/13T 1000KV
     120 VRMS, 60 Hz        SPWM V/f lazo abierto
```

Los canales son **mutuamente excluyentes**. Los negativos de las dos fuentes de potencia nunca se unen.

## 3. Canal 1: inversor monofásico

| Parámetro | Valor vigente |
|---|---|
| Fuente | 200 VDC, 40 W máximo, bus exclusivo de CH1 |
| Diseño nominal | 640 VA (sustentado con cálculo y simulación) |
| Salida | 120 VRMS, 60 Hz, índice de modulación ≈ 0.85 |
| Topología | Puente H, SPWM unipolar |
| Conmutadores | 4x IRFP360 (TO-247) |
| Conmutación | 20 kHz |
| Filtro | LC, Cf = 6.8 µF, Lf ≈ 1.15 mH (en revisión, ver pendientes) |
| DC-link | 470 µF / 400 V + 1.0 µF polipropileno + purga 100 kΩ / 2 W |
| Objetivo de THD | 10 % |

No se incluye etapa elevadora (boost) hacia 200 V: está fuera del alcance del anteproyecto,
que parte de una entrada DC disponible.

## 4. Canal 2: inversor trifásico

| Parámetro | Valor vigente |
|---|---|
| Fuente | PeakTech 6145 con conectores propios. 12 V para el motor, hasta 24 V para cargas RL |
| Topología | Puente trifásico de dos niveles |
| Conmutadores | 6x IRLZ44A (TO-220) |
| Modulación | SPWM con ley V/f en lazo abierto, sin realimentación |
| Fundamental | ~10 Hz a ~1.2 kHz |
| Conmutación | Seleccionable de 10 kHz a 20 kHz |
| Carga | Motor A2212/13T 1000KV, 14 polos (7 pares) |
| Pruebas | Con y sin hélice, con y sin carga. Requiere rampa de aceleración y corte por sobrecorriente |

## 5. Drivers

- 5 módulos IR2110 idénticos (2 para CH1, 3 para CH2). Se eligió por disponibilidad comercial.
- Bootstrap: 2x 1.0 µF en paralelo + diodo ultrarrápido UF4005/UF4007. Ciclo de trabajo limitado al 95 %.
- Resistencias de descarga de 10 kΩ en HO-VS y en LO-COM de cada IR2110.
- Tiempo muerto: 800 ns, generado por el MCPWM del ESP32-S3.
- El IR2110 no es aislado. El aislamiento se entiende como aislamiento del usuario respecto a la
  potencia: sensores ACS712 aislados, aislador USB o portátil a batería, carcasa cerrada, bornes IEC 61010.

## 6. Control: ESP32-S3-N16R8

Reemplaza a la Raspberry Pi Pico desde el 17 de octubre de 2026. La PCB de control y el firmware
se diseñan desde cero.

### Asignación de pines aprobada

Pines descartados: 0, 3, 45, 46 (arranque), 19, 20 (USB nativo), 26 a 37 (flash y PSRAM octal),
43, 44 (puente UART), y el pin del LED RGB.

| GPIO | Señal | Periférico |
|---|---|---|
| 1 | I_CH1 | ADC1_CH0 |
| 2 | I_U | ADC1_CH1 |
| 4 | I_V | ADC1_CH3 |
| 5 | EN_CH1 | GPIO |
| 6 | EN_CH2 | GPIO |
| 7 | Libre (opción: paro de emergencia físico) | GPIO |
| 8 | T_IRQ | GPIO (opcional) |
| 9 | TFT_DC | GPIO |
| 10 | TFT_CS | SPI2 IO_MUX |
| 11 | MOSI | SPI2 IO_MUX |
| 12 | SCK | SPI2 IO_MUX |
| 13 | MISO (solo T_DO) | SPI2 IO_MUX |
| 14 | T_CS | GPIO |
| 15 | HIN_A CH1 | MCPWM0 op0 |
| 16 | LIN_A CH1 | MCPWM0 op0 |
| 17 | HIN_B CH1 | MCPWM0 op1 |
| 18 | LIN_B CH1 | MCPWM0 op1 |
| 39 | HIN_U CH2 | MCPWM1 op0 |
| 40 | LIN_U CH2 | MCPWM1 op0 |
| 41 | HIN_V CH2 | MCPWM1 op1 |
| 42 | LIN_V CH2 | MCPWM1 op1 |
| 47 | HIN_W CH2 | MCPWM1 op2 |
| 21 | LIN_W CH2 | MCPWM1 op2 |
| 38 o 48 | Libre | El que no use el LED RGB |

TFT_RST al pin EN de la placa. TFT_BL a 3.3 V. SDO del TFT sin conectar.

### Interfaz de usuario

Toda en la TFT ILI9341 de 2.8" con táctil XPT2046. Sin botones ni selectores físicos.

## 7. Sensado y protección

- Solo se miden corrientes. Las tensiones de los buses DC no se miden.
- Un sensor en CH1 y dos en CH2 (fases U y V). La fase W se calcula como −(iU + iV).
- Divisor 10 kΩ / 20 kΩ (relación 2/3) hacia el ADC. Solo ADC1 (ADC2 choca con Wi-Fi),
  con calibración `esp_adc_cali`.
- **Protección por software:** el ESP32-S3 compara cada muestra con el umbral del canal activo
  (±2 A en CH1, ±15 A en fases U y V de CH2). Si se supera, deshabilita los drivers (EN) y detiene el MCPWM.
  El fusible de cada canal queda como respaldo.
- El shoot-through no se detecta. Se documenta como limitación.
- La protección es para seguridad y visualización, no para análisis de armónicos.

## 8. Alimentación y tierras

- Fuente de 15 V para los drivers. Regulador de 5 V en la PCB de control.
- La GND lógica es el VSS de los cinco IR2110. La desconexión física del canal inactivo debe abrir
  su negativo para no unir los dos negativos a través de la lógica.

## 9. Hardware y fabricación

- El hardware se trabaja en módulos independientes (driver, potencia, control).
- Archivo fuente: `hardware/easyeda/Archivo PCB.epro2` (EasyEDA Pro).

## 10. Verificación

- Sin analizador de potencia. Mediciones con multímetros y osciloscopio con FFT.
- El plan de pruebas no se ata a un instrumento específico.
- Criterios de aceptación al mínimo necesario y no más estrictos que los requisitos.
- La temperatura de los transistores no es criterio de verificación.

## 11. Documento de grado

- Documento: `Documento_Proyecto_Grado_Herrera_Marentes` en Google Drive, carpeta "Tesis DC-AC Inversor 2026".
- 8 capítulos + anexos, numeración escrita a mano en los títulos.
- Estilo: tiempo pasado, punto medio entre académico e informal, explicativo, cada párrafo
  autoconcluyente, vocabulario técnico usual en la ingeniería colombiana, números de cuatro cifras
  sin separador de miles. Sin precios fuera del capítulo del BOM.
- Figuras: generadas en Python por Claude; los anexos se mantienen como scripts de MATLAB,
  siempre que los resultados coincidan.

## 12. Pendientes abiertos

- [ ] Confirmar el modelo exacto de la placa ESP32-S3 (DevKitC-1 original o clon) para ubicar el LED RGB (GPIO38 o GPIO48).
- [ ] Confirmar el sensor de CH1: `CAMBIOS.md` (17 oct.) registra ACS712-05B; otra nota registra ACS712-20A para los tres.
- [ ] Revisar el filtro LC de CH1: Cf = 6.8 µF consume ≈ 0.31 A reactivos a 60 Hz, comparable con la corriente de carga a 40 W.
- [ ] Tiempo muerto de 800 ns: la simulación da 114.6 VRMS y THD 2.4 %. Decidir entre ma ≈ 0.89 o compensación en firmware.
- [ ] Medir el tiempo de reacción de la protección por software.
- [ ] Actualizar en el documento: matriz de selección del microcontrolador, diagrama de bloques
      (todavía muestra el elevador 64 V a 200 V y el RP2040), descripción del módulo de control y nota de sensado.
- [ ] Corregir errores de suma en la matriz de selección del driver (3.65 / 3.60 / 4.00 con la primera ponderación).
- [ ] Diseñar la PCB de control.
- [ ] Escribir el firmware del ESP32-S3.
