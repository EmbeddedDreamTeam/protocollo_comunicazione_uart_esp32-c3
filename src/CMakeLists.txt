# Seleziona sorgenti in base al target
# NON ELIMINARE NON È AUTOGENERATO DA PLATFORMIO
if(CONFIG_IDF_TARGET_ESP32C3)
    FILE(GLOB_RECURSE app_sources 
        ${CMAKE_SOURCE_DIR}/src/esp32-c3/*.* 
        ${CMAKE_SOURCE_DIR}/lib/*.*)
elseif(CONFIG_IDF_TARGET_ESP32S3)
    FILE(GLOB_RECURSE app_sources 
        ${CMAKE_SOURCE_DIR}/src/esp32-s3/*.* 
        ${CMAKE_SOURCE_DIR}/lib/*.*)
endif()

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "../include")