# Componente "main" del firmware. Registra los archivos fuente y las
# bibliotecas de ESP-IDF que necesitan.
idf_component_register(
    # Código del equipo: arranque, máquina de estados, modulación, potencia,
    # medición, pantalla, interfaz y modo de banco
    SRCS "main.c" "estado.c" "spwm.c" "potencia.c" "medicion.c" "pantalla.c" "ui.c" "banco.c"
         # Fuentes de la interfaz generadas con lv_font_conv (no se editan a mano)
         "fuentes/fuente_10.c" "fuentes/fuente_12.c" "fuentes/fuente_14.c"
         "fuentes/fuente_16.c" "fuentes/fuente_28.c"
         "fuentes/fuente_negrita_10.c" "fuentes/fuente_negrita_14.c" "fuentes/fuente_negrita_16.c" "fuentes/fuente_negrita_20.c"
    # Los #include "config.h" y similares se buscan en esta misma carpeta
    INCLUDE_DIRS "."
    # Componentes de ESP-IDF: MCPWM, GPIO, SPI, ADC, LCD, temporizador, LVGL
    # y el driver del ILI9341
    REQUIRES esp_driver_mcpwm esp_driver_gpio esp_driver_spi esp_adc esp_lcd
             esp_timer lvgl esp_lcd_ili9341
)

# idf.py -B build_banco -DMODO_BANCO=1 build  -> modo de banco (PRUEBA_BANCO.md)
# Define MODO_BANCO=1 para todos los archivos del componente
if(MODO_BANCO)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE MODO_BANCO=1)
endif()
