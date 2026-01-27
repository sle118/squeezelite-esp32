file(GLOB AACDEC_SOURCES "src/*.c")
file(GLOB AACDEC_HEADERS "src/*.h" "oscl/*.h" "include/*.h")

add_library(opencore-aacdec STATIC ${AACDEC_SOURCES})
if(NOT MSVC)
	target_compile_options(opencore-aacdec PRIVATE -Wno-array-parameter)
endif()	
add_definitions(-DAAC_PLUS -DHQ_SBR -DPARAMETRICSTEREO -DC_EQUIVALENT)
target_include_directories(opencore-aacdec PUBLIC "src/" "oscl/" "include/")