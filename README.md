# Tesis: Inversor DC-AC didáctico de doble canal

Universidad El Bosque, Ingeniería Electrónica, 2026.
Autores: Camilo Andrés Herrera Hoyos y Mariapaula Marentes Niño. Director: Ing. Fredy Rolando García Bello.

Inversor de dos canales mutuamente excluyentes, controlado por un ESP32-S3-N16R8:

- **Canal 1:** monofásico, puente H con IRFP360, 120 VRMS / 60 Hz desde un bus de 200 VDC.
- **Canal 2:** trifásico, puente de dos niveles con IRLZ44A, SPWM V/f en lazo abierto para un motor A2212/13T.

## Dónde está cada cosa

| Carpeta / archivo | Contenido |
|---|---|
| `CONTEXTO_PROYECTO.md` | Estado vigente del diseño. Empezar aquí. |
| `CAMBIOS.md` | Historial de decisiones con fecha. |
| `AGENTS.md` | Reglas para Claude, Antigravity y el equipo. |
| `hardware/easyeda/` | Proyecto EasyEDA Pro (`.epro2`). |
| `hardware/diagramas/` | Diagrama de bloques. |
| `firmware/` | Código del ESP32-S3 (por escribir). |
| `simulaciones/` | Scripts de MATLAB y Python. |
| `documento/` | Anteproyecto y exportaciones del documento de grado. |
| `datasheets/` | Hojas de datos de los componentes evaluados. |
| `referencias/` | Notas de aplicación y guías de diseño de PCB. |
| `docs/historico/` | Contexto anterior (versión con Raspberry Pi Pico). No usar como fuente de decisiones. |
