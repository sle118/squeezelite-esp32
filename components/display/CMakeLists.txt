# the JPEG library is in ROM but seems to fail randomly (PSRAM issue?)
set(TJPGD tjpgd)

idf_component_register(SRC_DIRS . core core/ifaces 
						INCLUDE_DIRS . core
						REQUIRES platform_config tools esp_common spiffs
						PRIV_REQUIRES services freertos driver ${TJPGD}
						EMBED_FILES note.jpg )		

if (NOT TJPGD)
add_compile_definitions(TJPGD_ROM)												
endif()

set_source_files_properties(display.c
    PROPERTIES COMPILE_FLAGS
   -Wno-format-overflow )
