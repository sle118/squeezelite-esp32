cmake_minimum_required(VERSION 3.16)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/extra")

set(TOOLS_DIR "${CMAKE_SOURCE_DIR}/tools" )
set(GENERATED_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/generated")
set(GENERATED_PY_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/py")
set(GENERATED_JS_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/js")



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
        set(PROTOBUF_INCLUDE_DIR "${TOOLS_DIR}/protobuf/win64/include/google/protobuf" PARENT_SCOPE)
        set(PROTOC_PLUGIN_SUFFIX ".bat" PARENT_SCOPE)
    else()
        set(PROTODOT_BINARY "${TOOLS_DIR}/protodot/binaries/protodot-linux-amd64" PARENT_SCOPE)
        set(CONFIG_FILE "${TOOLS_DIR}/protodot/config.json" PARENT_SCOPE)
        set(PROTOC_BINARY "${TOOLS_DIR}/protobuf/linux-x86_64/bin/protoc" PARENT_SCOPE)
        set(PROTOBUF_INCLUDE_DIR "${TOOLS_DIR}/protobuf/linux-x86_64/include/google/protobuf" PARENT_SCOPE)
        set(PROTOC_PLUGIN_SUFFIX ".py" PARENT_SCOPE)
  #  else()
  #      message(FATAL_ERROR "Unsupported operating system")
    endif()
endfunction()

set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    APPEND PROPERTY
    ADDITIONAL_MAKE_CLEAN_FILES "${CMAKE_CURRENT_BINARY_DIR}/protobuf"
)

set(PROTO_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/proto;${NANOPB_GENERATOR_SOURCE_DIR}/proto" )