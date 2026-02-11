idf_component_register( SRC_DIRS .
						INCLUDE_DIRS .   
						PRIV_REQUIRES bootloader_support esp_app_format json
 )
 
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=esp_app_desc")	
idf_build_get_property(project_ver PROJECT_VER)
message("******************* ${project_ver}")
string(SUBSTRING "${project_ver}" 0 31 PROJECT_VER_CUT)
set_source_files_properties(recovery.c PROPERTIES COMPILE_DEFINITIONS "PROJECT_VER=\"${PROJECT_VER_CUT}\"")
