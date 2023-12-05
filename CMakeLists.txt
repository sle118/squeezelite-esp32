cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(PROJECT_VER $ENV{PROJECT_VER})
add_definitions(-DMODEL_NAME=SqueezeESP32)

if(NOT DEFINED DEPTH)
	set(DEPTH "16")
endif()	

# State machine hierarchy enabled and logging enabled
add_definitions(-DSTATE_MACHINE_LOGGER=1)
add_definitions(-DHIERARCHICAL_STATES=1)

# Uncomment line below to get memory usage trace details
# add_definitions(-DENABLE_MEMTRACE=1)
#uncomment line below to get network ethernet debug logs 
#add_definitions(-DNETWORK_ETHERNET_LOG_LEVEL=ESP_LOG_DEBUG)
#uncomment line below to get network status debug logs
#add_definitions(-DNETWORK_STATUS_LOG_LEVEL=ESP_LOG_DEBUG)
#add_definitions(-DNETWORK_HANDLERS_LOG_LEVEL=ESP_LOG_DEBUG)
#add_definitions(-DNETWORK_WIFI_LOG_LEVEL=ESP_LOG_DEBUG)
#add_definitions(-DNETWORK_MANAGER_LOG_LEVEL=ESP_LOG_DEBUG)
#add_definitions(-DNETWORK_HTTP_SERVER_LOG_LEVEL=ESP_LOG_DEBUG)

# utility to build sizes
function(build_size target_name) 
    set(target_elf ${target_name}.elf)
    set(target_map ${target_name}.map)
    set(idf_size ${python} ${IDF_PATH}/tools/idf_size.py)
    
    if(DEFINED OUTPUT_JSON AND OUTPUT_JSON)
        list(APPEND idf_size "--json")
    endif()
 
    add_custom_target(size-${target_name} ALL
        DEPENDS ${target_elf}
        COMMAND ${idf_size} ${target_map} -o "size-${target_name}"
    )

    add_custom_target(size-files-${target_name} ALL
        DEPENDS ${target_elf}
        COMMAND ${idf_size} --files ${target_map}
    )

    add_custom_target(size-components-${target_name} ALL
        DEPENDS ${target_elf}
        COMMAND ${idf_size} --archives ${target_map}
    )
endfunction()  

# manually add the 2 versions for application: recovery and squeezelite
set(EXTRA_COMPONENT_DIRS components/platform_console/app_recovery components/platform_console/app_squeezelite  )

project(recovery)
spiffs_create_partition_image(spiffs  spiffs  FLASH_IN_PROJECT DEPENDS generate_spiffs_bin )

# we need own "esp_app_desc" to take precedence
add_custom_command(
			TARGET recovery.elf
			PRE_LINK
			COMMAND xtensa-esp32-elf-objcopy --weaken-symbol esp_app_desc  ${BUILD_DIR}/esp-idf/app_update/libapp_update.a
	        VERBATIM
)

# when building recovery, add app_recovery to the link
get_target_property(BCA recovery.elf LINK_LIBRARIES)
list(REMOVE_ITEM BCA "idf::app_squeezelite" "idf::app_recovery" "-Wl,--Map=${BUILD_DIR}/recovery.map" "-Wl,--cref -Wl,--Map=${BUILD_DIR}/recovery.map")
set_target_properties(recovery.elf PROPERTIES LINK_LIBRARIES "${BCA};idf::app_recovery;-Wl,--Map=${BUILD_DIR}/recovery.map")

# create files with size for recovery 
# build_size(recovery)

# build squeezelite, add app_squeezelite to the link
add_executable(squeezelite.elf "CMakeLists.txt")
add_dependencies(squeezelite.elf recovery.elf)
set_target_properties(squeezelite.elf PROPERTIES LINK_LIBRARIES "${BCA};idf::app_squeezelite;-Wl,--Map=${BUILD_DIR}/squeezelite.map")
get_target_property(BCASQUEEZELITE squeezelite.elf LINK_LIBRARIES)
add_custom_command(
			TARGET squeezelite.elf
			POST_BUILD 
			COMMAND ${CMAKE_COMMAND} -E echo "Generating ${BUILD_DIR}/squeezelite.bin"
            COMMAND ${CMAKE_COMMAND} -E echo "BCA value for squeezelite is: ${BCASQUEEZELITE}"
			COMMAND ${ESPTOOLPY} elf2image ${ESPTOOLPY_FLASH_OPTIONS} ${ESPTOOLPY_ELF2IMAGE_OPTIONS} -o "squeezelite.bin" "squeezelite.elf"
	        VERBATIM
)

# create files with size for squeezelite
# build_size(squeezelite)
    
# make it part of cleanup    
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" 
             APPEND PROPERTY 
             ADDITIONAL_MAKE_CLEAN_FILES "${BUILD_DIR}/squeezelite.elf" "${BUILD_DIR}/squeezelite.map"
)

# adding OTA_0 partition
partition_table_get_partition_info(otaapp_offset "--partition-type app --partition-subtype ota_0" "offset")
idf_component_get_property(main_args esptool_py FLASH_ARGS)
idf_component_get_property(sub_args esptool_py FLASH_SUB_ARGS)
esptool_py_flash_target(squeezelite-flash "${main_args}" "${sub_args}")
esptool_py_flash_target_image(squeezelite-flash squeezelite "${otaapp_offset}" "${BUILD_DIR}/squeezelite.bin")
esptool_py_flash_target_image(flash squeezelite "${otaapp_offset}" "${BUILD_DIR}/squeezelite.bin")

# and JTAG scripts
add_custom_target(_jtag_scripts  ALL
					BYPRODUCTS "flash_dbg_project_args"  
					POST_BUILD
					COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/generate_debug_scripts.cmake"
)

if(CMAKE_HOST_UNIX)
  # Add custom target to set executable permissions before build for cspot component
    add_custom_target(set_cspot_permissions
        COMMAND ${CMAKE_COMMAND} -E echo "************************************************************************************************"
        COMMAND ${CMAKE_COMMAND} -E echo "**** Setting permissions for required files"
        COMMAND ${CMAKE_COMMAND} -E echo "**** ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/protoc-gen-nanopb"
        COMMAND ${CMAKE_COMMAND} -E echo "**** ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/*.py"
        COMMAND ${CMAKE_COMMAND} -E echo "**** ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/*.py2"
        COMMAND ${CMAKE_COMMAND} -E echo "**** ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/proto/*.py"
        COMMAND ${CMAKE_COMMAND} -E echo "**** ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/protoc"
        COMMAND chmod +x ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/protoc-gen-nanopb
        COMMAND chmod +x ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/*.py
        COMMAND chmod +x ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/*.py2
        COMMAND chmod +x ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/proto/*.py
        COMMAND chmod +x ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/protoc
        COMMAND ${CMAKE_COMMAND} -E echo "************************************************************************************************"    
    )

  # Add a dependency to ensure permissions are set before building cspot component
  add_dependencies(__idf_spotify set_cspot_permissions)
endif()

# Set the main project library target here before including protocol_buffers.cmake
# add_library(main_project_lib ...)

# Include the protocol_buffers.cmake file
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/protobuf ${CMAKE_CURRENT_BINARY_DIR}/protobuf)


# ======================= DEBUG FLAGS ============================

#target_compile_definitions(__idf_esp_eth PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_INFO)

target_compile_definitions(__idf_services PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)

target_compile_definitions(__idf_driver PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_wifi-manager PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_wifi PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_platform_console PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)

target_compile_definitions(__idf_app_recovery PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_INFO)
target_compile_definitions(__idf_esp_eth PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_INFO)
target_compile_definitions(__idf_esp_event PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_INFO)
target_compile_definitions(__idf_esp_netif PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
 
target_compile_definitions(__idf_freertos PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_bt PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_ERROR)
target_compile_definitions(__idf_mdns PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_tcpip_adapter PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_tcp_transport PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)

target_compile_definitions(__idf_app_squeezelite PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_app_trace PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_app_update PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_asio PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_audio PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_bootloader_support PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)

target_compile_definitions(__idf_cbor PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_cmock PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_coap PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_console PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_cxx PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_display PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_driver PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_driver_bt PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_efuse PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp-dsp PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp-tls PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp32 PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_espcoredump PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_adc_cal PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_common PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_gdbstub PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_hid PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_https_ota PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_http_client PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_http_server PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_hw_support PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_ipc PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_local_ctrl PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_pm PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_ringbuf PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_rom PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_serial_slave_link PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_system PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_esp_timer PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_expat PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_fatfs PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_freemodbus PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_hal PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_heap PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_jsmn PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_json PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_libsodium PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_log PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_lwip PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_main PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_mbedtls PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(mbedcrypto PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(mbedtls PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(mbedx509 PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_mqtt PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_newlib PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_nghttp PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_nvs_flash PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_openssl PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_perfmon PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_platform_config PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_protobuf-c PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_protocomm PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_pthread PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_raop PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_sdmmc PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)

target_compile_definitions(__idf_soc PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_spiffs PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_spi_flash PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_squeezelite PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_squeezelite-ota PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_telnet PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_tools PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_ulp PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_unity PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_vfs PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_wear_levelling PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
target_compile_definitions(__idf_wifi_provisioning PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_wpa_supplicant PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_xtensa PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)