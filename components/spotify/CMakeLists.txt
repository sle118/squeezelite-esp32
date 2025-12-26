# this must be set *before* idf_component_register
set(CMAKE_CXX_STANDARD 20)

idf_component_register(
		SRC_DIRS . 
		INCLUDE_DIRS . "cspot/include"
		PRIV_REQUIRES mbedtls mdns nvs_flash platform_config services esp_http_server tools codecs
		LDFRAGMENTS "linker.lf"
)

		#INCLUDE_DIRS . "cspot/include" "cspot/bell/include"

add_definitions(-Wno-unused-variable -Wno-unused-const-variable -Wchar-subscripts -Wunused-label -Wmaybe-uninitialized -Wmisleading-indentation)

set(BELL_DISABLE_CODECS ON)
set(BELL_DISABLE_SINKS ON)
set(BELL_DISABLE_FMT ON)
set(BELL_DISABLE_REGEX ON)
set(BELL_ONLY_CJSON ON)
set(BELL_DISABLE_MQTT ON)
set(BELL_DISABLE_WEBSERVER ON)
set(CSPOT_TARGET_ESP32 ON)

set(_secret $ENV{SPOTIFY_SECRET})
if(_secret)
  separate_arguments(_secret)
  set_source_files_properties(shim.cpp PROPERTIES COMPILE_OPTIONS "${_secret}")
endif()

# because CMake is so broken, the cache set below overrides a normal "set" for the first build
set(BELL_EXTERNAL_VORBIS "idf::codecs" CACHE STRING "provide own codecs")
set(BELL_EXTERNAL_CJSON "idf::json" CACHE STRING "provide own CJSON")

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cspot ${CMAKE_CURRENT_BINARY_DIR}/cspot)
target_link_libraries(${COMPONENT_LIB} PRIVATE cspot ${EXTRA_REQ_LIBS})

