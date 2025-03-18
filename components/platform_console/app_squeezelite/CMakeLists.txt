idf_build_get_property(idf_path IDF_PATH)
idf_component_register( SRCS cmd_squeezelite.c 
						INCLUDE_DIRS . 
						PRIV_REQUIRES spi_flash bootloader_support  partition_table bootloader_support console codecs squeezelite newlib pthread tools platform_config display  services)
						

target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=feof")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=log")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=fdopen")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=fileno")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=fstat")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=lround")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=esp_app_desc")

idf_build_get_property(project_ver PROJECT_VER)
message("******************* ${project_ver}")
string(SUBSTRING "${project_ver}" 0 31 PROJECT_VER_CUT)
set_source_files_properties(cmd_squeezelite.c PROPERTIES COMPILE_DEFINITIONS "PROJECT_VER=\"${PROJECT_VER_CUT}\" ")


