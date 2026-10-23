# Copia del componente espressif/esp_lcd_ili9341 v2.1.0 (Apache-2.0, ver license.txt),
# sin la dependencia cmake_utilities, que solo agrega el número de versión.
idf_component_register(SRCS "esp_lcd_ili9341.c"
                       INCLUDE_DIRS "include"
                       REQUIRES "esp_lcd"
                       PRIV_REQUIRES "driver")
