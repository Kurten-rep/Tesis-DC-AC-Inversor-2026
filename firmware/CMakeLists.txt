# Firmware del inversor DC-AC didáctico de doble canal (ESP32-S3-N16R8)
cmake_minimum_required(VERSION 3.16)

# Componentes extra opcionales (por ejemplo, una copia local de LVGL cuando
# no hay acceso al registro de componentes de Espressif).
if(DEFINED ENV{FW_COMPONENTES_EXTRA})
    set(EXTRA_COMPONENT_DIRS $ENV{FW_COMPONENTES_EXTRA})
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(inversor_dc_ac)
