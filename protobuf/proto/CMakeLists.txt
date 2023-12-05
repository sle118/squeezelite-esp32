# Append nanopb path to CMAKE_MODULE_PATH for finding the nanopb package
cmake_minimum_required(VERSION 3.16)
include(../protobuf_utils.cmake)
configure_env()

file(GLOB_RECURSE  PROTO_FILES *.proto)
# set(NANOPB_OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}")
nanopb_generate_cpp(PROTO_SRCS PROTO_HDRS RELPATH ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_FILES})


# Create a custom target to generate the proto files
set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES GENERATED TRUE)
add_custom_target(generate_system_proto DEPENDS ${PROTO_SRCS} ${PROTO_HDRS})


add_library(platform_protobuf STATIC ${PROTO_SRCS} ${PROTO_HDRS})
# PUBLIC to propagate includes from bell to cspot dependents
target_compile_definitions(platform_protobuf PUBLIC PB_ENABLE_MALLOC)
target_compile_definitions(platform_protobuf PUBLIC PB_FIELD_32BIT)
target_include_directories(platform_protobuf PUBLIC "include" ${CMAKE_CURRENT_BINARY_DIR} ${NANOPB_INCLUDE_DIRS} ${EXTRA_INCLUDES})



# Link the generated proto library to your main targets
target_link_libraries(__idf_platform_config PRIVATE platform_protobuf)
target_link_libraries(__idf_wifi-manager PRIVATE platform_protobuf)
target_include_directories(__idf_platform_config PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/proto)



# Add nanopb includes
set(CMAKE_INCLUDE_GLOBAL_DIRS ON)

# include_directories("${CMAKE_CURRENT_BINARY_DIR}" "${NANOPB_INCLUDE_DIRS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I")
idf_build_set_property(INCLUDE_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR} APPEND)
foreach(PATH_ENTRY ${NANOPB_INCLUDE_DIRS})
    idf_build_set_property(INCLUDE_DIRECTORIES ${PATH_ENTRY} APPEND)
endforeach()

set(NANOPB_GENERATOR_SOURCE_DIR "${NANOPB_GENERATOR_SOURCE_DIR}" PARENT_SCOPE)
set(PROTO_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/proto;${NANOPB_GENERATOR_SOURCE_DIR}/proto" PARENT_SCOPE)
set(PROTO_FILES "${PROTO_FILES}" PARENT_SCOPE)
set(PROTO_SRCS "${PROTO_SRCS}" PARENT_SCOPE)
set(PROTO_HDRS "${PROTO_HDRS}" PARENT_SCOPE)

