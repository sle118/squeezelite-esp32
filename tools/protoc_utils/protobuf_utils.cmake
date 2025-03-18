cmake_minimum_required(VERSION 3.16)
# Set a variable to the project root directory
# Function to find the project root directory by looking for the "tools" directory
function(find_project_root_dir start_dir project_root)
    set(next_dir ${start_dir})
    set(found FALSE)

    while(NOT found AND NOT "${next_dir}" STREQUAL "")
        message(STATUS "Checking ${next_dir} for sdkconfig")
        if(EXISTS "${next_dir}/sdkconfig")
            set(found TRUE)
            set(${project_root} ${next_dir} PARENT_SCOPE)
        else()
            get_filename_component(next_dir ${next_dir} DIRECTORY)
        endif()
    endwhile()

    if(NOT found)
        message(FATAL_ERROR "Unable to find the project root directory containing 'sdkconfig'.")
    endif()
endfunction()

# Call the function to find the project root directory
find_project_root_dir(${CMAKE_CURRENT_SOURCE_DIR} PROJECT_ROOT_DIR)


list(APPEND CMAKE_MODULE_PATH "${PROJECT_ROOT_DIR}/components/spotify/cspot/bell/external/nanopb/extra")

set(TOOLS_DIR "${PROJECT_ROOT_DIR}/tools" )
set(GENERATED_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/generated")
set(GENERATED_PROTOBUF_ROOT "${CMAKE_BINARY_DIR}/protobuf")
set(GENERATED_PY_DIRECTORY "${GENERATED_PROTOBUF_ROOT}/py")
set(GENERATED_JS_DIRECTORY "${GENERATED_PROTOBUF_ROOT}/js")
set(GENERATED_SPIFFS_DIRECTORY "${CMAKE_BINARY_DIR}/spiffs")


find_package(PythonInterp REQUIRED)
# Function to replace a placeholder in a list
function(replace_in_list INPUT_LIST PLACEHOLDER REPLACEMENT OUTPUT_LIST)
    set(TEMP_LIST "")
    foreach(ITEM ${${INPUT_LIST}})
        string(REPLACE ${PLACEHOLDER} ${REPLACEMENT} ITEM ${ITEM})
        list(APPEND TEMP_LIST "${ITEM}")
    endforeach()
    set(${OUTPUT_LIST} ${TEMP_LIST} PARENT_SCOPE)
endfunction()

function(encode_special_chars INPUT_VAR)
    set(ENCODED_STRING "${${INPUT_VAR}}")

    # Encoding common special characters. Start with % so that
    # we don't collide with encodings if we process that char
    # later. 
    string(REPLACE "%" "%25" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE ":" "%3A" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "-" "%2D" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "=" "%3D" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "&" "%26" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "?" "%3F" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "/" "%2F" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE " " "%20" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "!" "%21" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "@" "%40" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "#" "%23" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "$" "%24" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "^" "%5E" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "*" "%2A" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "(" "%28" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE ")" "%29" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "+" "%2B" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "{" "%7B" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "}" "%7D" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "[" "%5B" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "]" "%5D" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "|" "%7C" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "\\" "%5C" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE ";" "%3B" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "'" "%27" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "\"" "%22" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "<" "%3C" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE ">" "%3E" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "," "%2C" ENCODED_STRING "${ENCODED_STRING}")
    string(REPLACE "`" "%60" ENCODED_STRING "${ENCODED_STRING}")
    # Add more replacements as needed

    # Set the result in the parent scope
    set(${INPUT_VAR} "${ENCODED_STRING}" PARENT_SCOPE)
endfunction()


function(array_to_delimited DELIMITER VAR_NAME)
    # Initialize the result variable
    set(RESULT "")
    set(ARR "${${VAR_NAME}}")
    # Determine if ARR is a list
    list(LENGTH ARR ARR_LENGTH)

    if(${ARR_LENGTH} GREATER 0)
        # Handle ARR as ITEMS
        foreach(ARRAY_ENTRY IN ITEMS ${ARR})
            set(RESULT "${RESULT}${ARRAY_ENTRY}${DELIMITER}")
        endforeach()
    else()
        # Handle ARR as a LIST
        foreach(ARRAY_ENTRY IN LISTS ${ARR})
            set(RESULT "${RESULT}${ARRAY_ENTRY}${DELIMITER}")
        endforeach()
    endif()

    # Remove the trailing delimiter
    # string(REGEX REPLACE "${DELIMITER}$" "" RESULT "${RESULT}")
    # encode_special_chars(RESULT)
    set(${VAR_NAME}_DELIMITED "${RESULT}" PARENT_SCOPE)
endfunction()




function(print_array MSG ARR)
    # Determine if ARR is a list
    list(LENGTH ARR ARR_LENGTH)

    # Check for the optional parameter to print each item on a new line
    list(LENGTH ARGN ARG_COUNT)

    
    if(ARG_COUNT EQUAL 1)
        # Get the first (and only) item in ARGN
        list(GET ARGN 0 ARGN_FIRST_ITEM)
    endif()
    if(ARG_COUNT EQUAL 1 AND ARGN_FIRST_ITEM STREQUAL "NEWLINE")
        if(${ARR_LENGTH} GREATER 0)
            message(STATUS "${MSG} [ITEMS]")
            foreach(ARRAY_ENTRY IN ITEMS ${ARR})
                message(STATUS "    - ${ARRAY_ENTRY}")
            endforeach()
        else()
            message(STATUS "${MSG} [LISTS]")
            foreach(ARRAY_ENTRY IN LISTS ${ARR})
                message(STATUS "    - ${ARRAY_ENTRY}")
            endforeach()
        endif()
    else()
        # Default behavior: concatenate the message and array entries
        set(OUTSTRING "")

        if(${ARR_LENGTH} GREATER 0)
            foreach(ARRAY_ENTRY IN ITEMS ${ARR})
                set(OUTSTRING "${OUTSTRING} ${ARRAY_ENTRY}")
            endforeach()
        else()
            foreach(ARRAY_ENTRY IN LISTS ${ARR})
                set(OUTSTRING "${OUTSTRING} ${ARRAY_ENTRY}")
            endforeach()
        endif()

        message(STATUS "${MSG}${OUTSTRING}")
    endif()
endfunction()

function(configure_env)
    if(CMAKE_HOST_WIN32)
        set(PROTODOT_BINARY "${TOOLS_DIR}/protodot/binaries/protodot-windows-amd64.exe" PARENT_SCOPE)
        set(CONFIG_FILE "${TOOLS_DIR}/protodot/config-win.json" PARENT_SCOPE)
        set(PROTOC_BINARY "${TOOLS_DIR}/protobuf/win64/bin/protoc.exe" PARENT_SCOPE)
        set(PROTOBUF_JS_BINARY "${TOOLS_DIR}/protobuf-javascript/win64/bin/protoc-gen-js.exe" PARENT_SCOPE)
        set(PROTOBUF_INCLUDE_DIR "${TOOLS_DIR}/protobuf/win64/include/google/protobuf" PARENT_SCOPE)
        set(PROTOC_PLUGIN_SUFFIX ".bat" PARENT_SCOPE)
    else()
        set(PROTODOT_BINARY "${TOOLS_DIR}/protodot/binaries/protodot-linux-amd64" PARENT_SCOPE)
        set(CONFIG_FILE "${TOOLS_DIR}/protodot/config.json" PARENT_SCOPE)
        set(PROTOC_BINARY "${TOOLS_DIR}/protobuf/linux-x86_64/bin/protoc" PARENT_SCOPE)
        set(PROTOBUF_JS_BINARY "${TOOLS_DIR}/protobuf-javascript/linux-x86_64/bin/protoc-gen-js" PARENT_SCOPE)
        set(PROTOBUF_INCLUDE_DIR "${TOOLS_DIR}/protobuf/linux-x86_64/include/google/protobuf" PARENT_SCOPE)
        set(PROTOC_PLUGIN_SUFFIX ".py" PARENT_SCOPE)
  #  else()
  #      message(FATAL_ERROR "Unsupported operating system")
    endif()
endfunction()

set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    APPEND PROPERTY
    ADDITIONAL_MAKE_CLEAN_FILES "${CMAKE_BINARY_DIR}/protobuf"
)

set(PROTO_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/proto;${NANOPB_GENERATOR_SOURCE_DIR}/proto" )

# Path to the marker file
set(MARKER_FILE "${CMAKE_BINARY_DIR}/python_requirements_met.txt")

# Check if the marker file exists
if(NOT EXISTS ${MARKER_FILE})
    # Marker file doesn't exist, run the Python script
    execute_process(
        COMMAND ${PYTHON_EXECUTABLE} ${TOOLS_DIR}/protoc_utils/check_python_packages.py ${MARKER_FILE}
        RESULT_VARIABLE PYTHON_PACKAGES_CHECK_RESULT
    )

    # Check the result of the Python script
    if(NOT PYTHON_PACKAGES_CHECK_RESULT EQUAL 0)
        message(FATAL_ERROR "Python package requirements not satisfied.")
    endif()
else()
    message(STATUS "Python package requirements already satisfied.")
endif()


function(copy_files SRC_FILES SRC_BASE_DIR DEST_BASE_DIR)
    foreach(SRC_FILE ${SRC_FILES})
        # Get the relative path of the source file
        file(RELATIVE_PATH RELATIVE_SRC_FILE "${SRC_BASE_DIR}" ${SRC_FILE})

        string(REPLACE "${CMAKE_BINARY_DIR}" "" TARGET_NAME ${DEST_BASE_DIR})
        string(REPLACE "/" "_" TARGET_NAME ${TARGET_NAME})
        string(REPLACE "\\" "_" TARGET_NAME ${TARGET_NAME})
        
        # Compute the destination file path
        set(DEST_FILE "${DEST_BASE_DIR}/${RELATIVE_SRC_FILE}")

        # Create the directory structure in the destination
        get_filename_component(DEST_DIR ${DEST_FILE} DIRECTORY)
        file(MAKE_DIRECTORY ${DEST_DIR})

        # Copy the file if different
        add_custom_command(
            OUTPUT ${DEST_FILE}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SRC_FILE} ${DEST_FILE}
            DEPENDS ${SRC_FILE}
            COMMENT "Copying ${RELATIVE_SRC_FILE} to ${DEST_BASE_DIR}"
        )
        list(APPEND COPIED_FILES ${DEST_FILE})
    endforeach()
    # Add a custom target to trigger the copy
    add_custom_target(copy_${TARGET_NAME} DEPENDS ${COPIED_FILES})
    set(CUSTOM_COPY_TARGET copy_${TARGET_NAME} PARENT_SCOPE)
    set_property(GLOBAL APPEND PROPERTY CUSTOM_COPY_TARGETS copy_${TARGET_NAME})

endfunction()
