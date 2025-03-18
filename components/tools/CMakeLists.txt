set(CMAKE_CXX_STANDARD 20)
idf_component_register( SRC_DIRS .
						REQUIRES esp_common pthread json spiffs
						PRIV_REQUIRES esp_http_client esp_http_server esp-tls services
						INCLUDE_DIRS .
)

#doing our own implementation of new operator for some pre-compiled binaries
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u _ZdlPv")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u _Znwj")
