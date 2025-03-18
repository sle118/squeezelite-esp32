set(CMAKE_CXX_STANDARD 20)
idf_component_register(SRC_DIRS .
						INCLUDE_DIRS . 
						PRIV_REQUIRES    newlib  console esp_common freertos  
						REQUIRES spiffs tools services esp_http_server
)
add_dependencies(${COMPONENT_LIB} generate_plugins_files)