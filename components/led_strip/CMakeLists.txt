
idf_component_register(SRC_DIRS .
						INCLUDE_DIRS .
						REQUIRES platform_config tools esp_common
						PRIV_REQUIRES services freertos driver           
)

set_source_files_properties(led_strip.c
    PROPERTIES COMPILE_FLAGS
   -Wno-format-overflow
)
