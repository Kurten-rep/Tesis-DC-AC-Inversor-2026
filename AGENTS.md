# Reglas de trabajo para asistentes (Claude y Antigravity)

Este repositorio lo editan dos asistentes y el equipo. Estas reglas evitan que uno pise el trabajo del otro.

## Antes de empezar
1. Hacer `git pull` para traer los cambios más recientes.
2. Leer `CONTEXTO_PROYECTO.md` (estado vigente) y las últimas entradas de `CAMBIOS.md`.
3. Ignorar `docs/historico/` como fuente de decisiones. Solo es referencia.

## Mientras se trabaja
- No asumir parámetros que no estén en `CONTEXTO_PROYECTO.md`. Si falta un dato, preguntar al equipo.
- Si una decisión cambia, actualizar `CONTEXTO_PROYECTO.md` **y** agregar una entrada en `CAMBIOS.md`
  con fecha, autor (Claude, Antigravity o nombre), qué cambió y qué reemplaza.
- No incluir precios fuera del capítulo del BOM.
- Unidades SI. Valores de componentes con su tolerancia.

## Al terminar
- Commits pequeños con mensaje en español que diga qué cambió, por ejemplo:
  `firmware: rampa de aceleración V/f para CH2`.
- `git pull --rebase` antes de `git push` para no sobrescribir cambios del otro asistente.
- Si hay conflicto en `CONTEXTO_PROYECTO.md` o `CAMBIOS.md`, conservar ambas versiones y preguntar.

## Archivos binarios
- `hardware/easyeda/Archivo PCB.epro2` es binario: Git no puede fusionar cambios. Solo una persona
  o herramienta lo edita a la vez. Avisar en `CAMBIOS.md` al subir una versión nueva.
- Los `.docx` del documento de grado viven en Google Drive. Aquí solo van exportaciones de respaldo.

## Estilo de redacción del documento de grado
Tiempo pasado, punto medio entre académico e informal, explicativo, cada párrafo autoconcluyente,
vocabulario técnico usual en la ingeniería colombiana, números de cuatro cifras sin separador de miles.
Sin términos grandilocuentes ("formalmente", "naturaleza", "vanguardia").
