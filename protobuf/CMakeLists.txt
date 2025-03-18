# Append nanopb path to CMAKE_MODULE_PATH for finding the nanopb package
cmake_minimum_required(VERSION 3.16)
message(STATUS "** PLATFORM PROTOBUF")
include(../tools/protoc_utils/protobuf_utils.cmake)

configure_env()
file(MAKE_DIRECTORY "${GENERATED_DIRECTORY}")
file(MAKE_DIRECTORY "${GENERATED_PY_DIRECTORY}")
file(MAKE_DIRECTORY "${GENERATED_JS_DIRECTORY}")
add_subdirectory(proto)




# Custom command to run protodot when a .proto file changes
foreach(PROTO_FILE IN ITEMS ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    set(GENERATED_PY "${GENERATED_PY_DIRECTORY}/${PROTO_NAME}_pb2.py")
    set(GENERATED_JS "${GENERATED_JS_DIRECTORY}/${PROTO_NAME}_pb2.js")
    set(GENERATED_DOT "${GENERATED_DIRECTORY}/${PROTO_NAME}.dot")

    # Prepare the protodot command and its arguments
    set(PROTODOT_COMMAND
    ${PROTODOT_BINARY}
    -src ${PROTO_FILE}
    -select "*"
    -config ${CONFIG_FILE}
    -output ${PROTO_NAME}
    -generated "${GENERATED_DIRECTORY}"
    -inc "${PROTO_INCLUDE_DIRECTORIES}"
    )
    add_custom_command(
        OUTPUT ${GENERATED_DOT} 
        COMMAND ${PROTODOT_COMMAND}
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating DOT file for ${PROTO_NAME}"
        VERBATIM
    )
    
    
    list(APPEND GENERATED_PY_FILES ${GENERATED_PY} )
    list(APPEND GENERATED_JS_FILES ${GENERATED_JS} )
    list(APPEND GENERATED_DOT_FILES ${GENERATED_DOT})
endforeach()

set(PROTOC_PATH_LIST 
--proto_path=${TOOLS_DIR}/protoc_utils
--proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
--proto_path=${NANOPB_GENERATOR_SOURCE_DIR}/proto
--proto_path=${CMAKE_CURRENT_BINARY_DIR}/proto/nanopb/generator/proto
)

# now generate python files 
set(PROTOC_PYTHON_ARGS
    --python_out=${GENERATED_PY_DIRECTORY}
    --plugin=protoc-gen-js=${PROTOBUF_JS_BINARY}
    --js_out=import_style=commonjs,binary:${GENERATED_JS_DIRECTORY}
    ${PROTOC_PATH_LIST}
    ${PROTO_FILES}
)

message(STATUS "Source generation args: ${PROTOC_PYTHON_ARGS}")

add_custom_command(
    OUTPUT ${GENERATED_PY_FILES} 
    COMMAND ${PROTOC_BINARY}
    ARGS ${PROTOC_PYTHON_ARGS}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating Python source ${GENERATED_PY_FILES} "
)
add_custom_target(generate_graph_files DEPENDS ${GENERATED_DOT_FILES} )
add_custom_target(generate_py_files DEPENDS ${GENERATED_PY_FILES} )
set_source_files_properties(${GENERATED_DOT_FILES} ${GENERATED_PY_FILES}  PROPERTIES GENERATED TRUE)
add_dependencies(generate_py_files generate_system_proto )



set(PROTOC_UTILS_DIR ${TOOLS_DIR}/protoc_utils)
set(GENERATED_PLUGIN_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins")
file(MAKE_DIRECTORY "${GENERATED_PLUGIN_DIRECTORY}")
set(ENV{PATH} "${PROTOC_UTILS_DIR};$ENV{PATH}")

if(CMAKE_HOST_UNIX)
    file(GLOB PLUGINS_EXECUTABLES "${PROTOC_UTILS_DIR}/protoc-gen-*")

    # Add custom target to set executable permissions for plugin executables
    add_custom_target(set_proto_plugins_permissions
        COMMAND ${CMAKE_COMMAND} -E echo "**** Setting permissions for required files"
        COMMAND chmod +x ${PLUGINS_EXECUTABLES}
        COMMAND ${CMAKE_COMMAND} -E echo "**** Permissions set for plugin executables"
        COMMAND ${CMAKE_COMMAND} -E echo "************************************************************************************************"
    )

    # Add a dependency to ensure permissions are set before building cspot component
    add_dependencies(generate_py_files set_proto_plugins_permissions)
endif()

set(GENERATED_BIN_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
set(PROTO_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/proto;${NANOPB_GENERATOR_SOURCE_DIR}/proto")
file(MAKE_DIRECTORY "${GENERATED_BIN_DIRECTORY}")

string(REPLACE ":" "%3A" GENERATED_PY_DIRECTORY_ENC ${GENERATED_PY_DIRECTORY})
string(REPLACE ":" "%3A" GENERATED_BIN_DIRECTORY_ENC ${GENERATED_BIN_DIRECTORY})
list(APPEND PROTOC_PATH_LIST "--proto_path=${GENERATED_PY_DIRECTORY}")
array_to_delimited("?" PROTOC_PATH_LIST )
string(REPLACE "--proto_path=" "" PROTOC_PATH_LIST_DELIMITED ${PROTOC_PATH_LIST_DELIMITED})

set(DEFAULTS_OUTPUT "${GENERATED_SPIFFS_DIRECTORY}/data/defaults/config.bin")

set(PLUGINS_OPT 
    --plugin=protoc-gen-ZZZZ=${PROTOC_UTILS_DIR}/protoc-gen-ZZZZ${PROTOC_PLUGIN_SUFFIX}
    --ZZZZ_out=main_class=sys.config!sys.status.data!sys.state.data:${GENERATED_PLUGIN_DIRECTORY}
    --ZZZZ_opt=options=customoptions
    --ZZZZ_opt=path=${PROTOC_PATH_LIST_DELIMITED}
    --ZZZZ_opt=binpath=${GENERATED_BIN_DIRECTORY}
    --ZZZZ_opt=const_prefix=squeezelite-
    --ZZZZ_opt=defaultspath=${GENERATED_SPIFFS_DIRECTORY}/data
)

replace_in_list(PLUGINS_OPT "ZZZZ" "dump" DUMP_OUT_OPTS)
replace_in_list(PLUGINS_OPT "ZZZZ" "json" DUMP_JSON_OPTS)
replace_in_list(PLUGINS_OPT "ZZZZ" "defaults" DUMP_DEFAULTS_OPTS)


set(PROTOC_ARGS
${DUMP_OUT_OPTS}
${DUMP_JSON_OPTS}
${DUMP_DEFAULTS_OPTS}
${PROTOC_PATH_LIST}
${PROTO_FILES}
)

# print_array("PROTOC Args:" "${PROTOC_ARGS}" "NEWLINE")

SET(PLUGINS_OUTPUT 
    "${GENERATED_BIN_DIRECTORY}/code_generator_request.bin" 
    "${GENERATED_PLUGIN_DIRECTORY}/sys.config_pb2.json"
    "${GENERATED_PLUGIN_DIRECTORY}/sys.state.data_pb2.json"
    "${GENERATED_PLUGIN_DIRECTORY}/sys.status.data_pb2.json"
    "${DEFAULTS_OUTPUT}"
)

add_custom_command(
    OUTPUT ${PLUGINS_OUTPUT}
    COMMAND ${PROTOC_BINARY}
    ARGS ${PROTOC_ARGS}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating various sources for ${PLUGINS_OUTPUT}"
)

add_dependencies(generate_graph_files generate_system_proto )
add_custom_target(generate_plugins_files DEPENDS ${PLUGINS_OUTPUT} )
add_dependencies(generate_plugins_files generate_py_files)
set_source_files_properties( ${PLUGINS_OUTPUT}  PROPERTIES GENERATED TRUE)
# add_dependencies(generate_spiffs_bin DEPENDS   ${DEFAULTS_OUTPUT})

message(STATUS "**")