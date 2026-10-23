idf_component_register(
    SRCS "main.c" "estado.c" "spwm.c" "potencia.c" "medicion.c" "pantalla.c" "ui.c" "banco.c"
         "fuentes/fuente_10.c" "fuentes/fuente_12.c" "fuentes/fuente_14.c"
         "fuentes/fuente_16.c" "fuentes/fuente_28.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_mcpwm esp_driver_gpio esp_driver_spi esp_adc esp_lcd
             esp_timer lvgl esp_lcd_ili9341
)

# idf.py -B build_banco -DMODO_BANCO=1 build  -> modo de banco (PRUEBA_BANCO.md)
if(MODO_BANCO)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE MODO_BANCO=1)
endif()
