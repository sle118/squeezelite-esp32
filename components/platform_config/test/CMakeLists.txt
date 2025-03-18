idf_component_register(SRC_DIRS .
                      INCLUDE_DIRS "."
                      REQUIRES  tools platform_config unity )
# target_compile_options(__idf_platform_config PRIVATE --coverage)
target_include_directories(${COMPONENT_LIB}  PUBLIC SYSTEM ${CMAKE_SOURCE_DIR}/test_main)

message(STATUS "** PLATFORM PROTOBUF")
include(../../../tools/protoc_utils/protobuf_utils.cmake)
configure_env()
file(GLOB  PROTO_FILES *.proto)
set(NANOPB_OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}" "-I${PROJECT_ROOT_DIR}/protobuf/proto")
nanopb_generate_cpp(PROTO_SRCS PROTO_HDRS RELPATH ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_FILES})


# Create a custom target to generate the proto files
set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES GENERATED TRUE)
add_custom_target(generate_test_proto ALL DEPENDS ${PROTO_SRCS} ${PROTO_HDRS})
target_sources(${COMPONENT_LIB} PRIVATE ${PROTO_SRCS})
add_dependencies(${COMPONENT_LIB} generate_test_proto) 
target_include_directories(${COMPONENT_LIB} PUBLIC "include" ${CMAKE_CURRENT_BINARY_DIR} ${NANOPB_INCLUDE_DIRS} ${EXTRA_INCLUDES})
