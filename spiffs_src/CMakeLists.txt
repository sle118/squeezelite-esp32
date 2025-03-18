cmake_minimum_required(VERSION 3.16)
message(STATUS "** SPIFFS ")
include(../tools/protoc_utils/protobuf_utils.cmake)


set(GENERATED_BIN_DIRECTORY "${GENERATED_SPIFFS_DIRECTORY}/data")
file(MAKE_DIRECTORY "${GENERATED_BIN_DIRECTORY}")

function(generate_protobuf_bin SOURCE_JSON_FILE PROTO_FILE MAIN_CLASS GENERATED_BIN_DIRECTORY)
    # Get the relative path from SOURCE_JSON_FILE
    file(RELATIVE_PATH RELATIVE_JSON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/" "${SOURCE_JSON_FILE}")
    get_filename_component(RELATIVE_DIR "${RELATIVE_JSON_PATH}" DIRECTORY)
    get_filename_component(STRUCTURE_NAME "${RELATIVE_JSON_PATH}" NAME_WE)

    # Create the directory structure in GENERATED_BIN_DIRECTORY
    set(TARGET_BIN_DIRECTORY "${GENERATED_BIN_DIRECTORY}/${RELATIVE_DIR}")
    file(MAKE_DIRECTORY "${TARGET_BIN_DIRECTORY}")

    # Set the path for the generated .bin file
    set(GENERATED_BIN "${TARGET_BIN_DIRECTORY}/${STRUCTURE_NAME}.bin")
    string(TOLOWER ${GENERATED_BIN} GENERATED_BIN)


    # Prepare the protodot command and its arguments
    set(PROTOBIN_COMMAND
        ${PYTHON_EXECUTABLE}
        ${TOOLS_DIR}/protoc_utils/generate_bin.py
        --proto_file "${PROTO_FILE}"
        --main_class "${MAIN_CLASS}"
        --target_dir "${TARGET_BIN_DIRECTORY}"
        --json ${SOURCE_JSON_FILE}
        --include "${GENERATED_PROTOBUF_ROOT}/proto/nanopb/generator/proto"
        --include "${GENERATED_PROTOBUF_ROOT}/py/"
    )

    add_custom_command(
        OUTPUT ${GENERATED_BIN}
        COMMAND ${PROTOBIN_COMMAND}
        DEPENDS ${SOURCE_JSON_FILE}
        COMMENT "Generating BIN file ${GENERATED_BIN} for structure ${STRUCTURE_NAME} from json ${SOURCE_JSON_FILE}"
        VERBATIM
    )

    set(GENERATED_BIN_FILES ${GENERATED_BIN_FILES} ${GENERATED_BIN} PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE SOURCE_JSON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/targets/*.json")
foreach(SOURCE_JSON_FILE IN LISTS SOURCE_JSON_FILES)
    generate_protobuf_bin("${SOURCE_JSON_FILE}" "${GENERATED_PROTOBUF_ROOT}/py/configuration" "config" "${GENERATED_BIN_DIRECTORY}")
endforeach()

generate_protobuf_bin("${CMAKE_CURRENT_SOURCE_DIR}/data/default_sets.json" "${GENERATED_PROTOBUF_ROOT}/py/DAC" "default_sets" "${GENERATED_BIN_DIRECTORY}")


add_custom_target(generate_spiffs_bin DEPENDS  ${GENERATED_BIN_FILES} )
set_source_files_properties(${GENERATED_BIN_FILES}  PROPERTIES GENERATED TRUE)
add_dependencies(generate_spiffs_bin generate_system_proto)
add_dependencies(generate_spiffs_bin generate_py_files)

# Copy .bin files from spiffs_src/data to GENERATED_SPIFFS_DIRECTORY/data
file(GLOB_RECURSE SPIFFS_SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/*.bin")
copy_files("${SPIFFS_SRC_FILES}" "${CMAKE_CURRENT_SOURCE_DIR}/data" "${GENERATED_SPIFFS_DIRECTORY}/data")
list(APPEND SPIFFS_DATA_FILES "${CUSTOM_COPY_TARGET}")

# Copy .gz and png files from components/wifi-manager/webapp/dist to GENERATED_SPIFFS_DIRECTORY/data/www
file(GLOB_RECURSE WIFI_MANAGER_FILES 
     "${PROJECT_ROOT_DIR}/components/wifi-manager/webapp/dist/*.gz"
     "${PROJECT_ROOT_DIR}/components/wifi-manager/webapp/dist/*.png")
copy_files("${WIFI_MANAGER_FILES}" "${PROJECT_ROOT_DIR}/components/wifi-manager/webapp/dist" "${GENERATED_SPIFFS_DIRECTORY}/data/www")
list(APPEND SPIFFS_DATA_FILES "${CUSTOM_COPY_TARGET}")

# Retrieve the list of custom target names
get_property(CUSTOM_COPY_TARGETS GLOBAL PROPERTY CUSTOM_COPY_TARGETS)

spiffs_create_partition_image(spiffs  ${GENERATED_SPIFFS_DIRECTORY}/data  FLASH_IN_PROJECT DEPENDS generate_spiffs_bin ${CUSTOM_COPY_TARGETS} )
message(STATUS "** ")