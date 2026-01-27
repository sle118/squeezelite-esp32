cmake_minimum_required(VERSION 2.8.12)
cmake_policy(SET CMP0077 NEW)

project(bell)

# Configurable options
option(BELL_DISABLE_CODECS "Disable the entire audio codec wrapper" OFF)
option(BELL_CODEC_AAC "Support opencore-aac codec" ON)
option(BELL_CODEC_MP3 "Support libhelix-mp3 codec" ON)
option(BELL_DISABLE_MQTT "Disable the built-in MQTT wrapper" OFF)
option(BELL_DISABLE_WEBSERVER "Disable the built-in Web server" OFF)
option(BELL_CODEC_VORBIS "Support tremor Vorbis codec" ON)
option(BELL_CODEC_ALAC "Support Apple ALAC codec" ON)
option(BELL_CODEC_OPUS "Support Opus codec" ON)
option(BELL_DISABLE_SINKS "Disable all built-in audio sink implementations" OFF)

# These are default OFF, as they're OS-dependent (ESP32 sinks are always enabled - no external deps)
option(BELL_SINK_ALSA "Enable ALSA audio sink" OFF)
option(BELL_SINK_PORTAUDIO "Enable PortAudio sink" OFF)

# cJSON wrapper
option(BELL_ONLY_CJSON "Use only cJSON, not Nlohmann")
set(BELL_EXTERNAL_CJSON "" CACHE STRING "External cJSON library target name, optional")

# vorbis
set(BELL_EXTERNAL_VORBIS "" CACHE STRING "External Vorbis library target name, optional")
option(BELL_VORBIS_FLOAT "Use floating point Vorbis API" OFF)

# fmt & regex
option(BELL_DISABLE_FMT "Don't use std::fmt (saves space)" OFF)
option(BELL_DISABLE_REGEX "Don't use std::regex (saves space)" OFF)

# disable json tests
set(JSON_BuildTests OFF CACHE INTERNAL "")

# Backwards compatibility with deprecated options
if(BELL_USE_ALSA)
    message(WARNING "Deprecated Bell options used, replace BELL_USE_ALSA with BELL_SINK_ALSA")
    set(BELL_SINK_ALSA ${BELL_USE_ALSA})
endif()

if(BELL_USE_PORTAUDIO)
    message(WARNING "Deprecated Bell options used, replace BELL_USE_PORTAUDIO with BELL_SINK_PORTAUDIO")
    set(BELL_SINK_PORTAUDIO ${BELL_USE_PORTAUDIO})
endif()

message(STATUS "Bell options:")
message(STATUS "    Disable all codecs: ${BELL_DISABLE_CODECS}")

if(NOT BELL_DISABLE_CODECS)
    message(STATUS "    - AAC audio codec: ${BELL_CODEC_AAC}")
    message(STATUS "    - MP3 audio codec: ${BELL_CODEC_MP3}")
    message(STATUS "    - Vorbis audio codec: ${BELL_CODEC_VORBIS}")
    message(STATUS "    - Opus audio codec: ${BELL_CODEC_OPUS}")
    message(STATUS "    - ALAC audio codec: ${BELL_CODEC_ALAC}")
endif()

message(STATUS "    Disable built-in audio sinks: ${BELL_DISABLE_SINKS}")
message(STATUS "    Use Vorbis float version: ${BELL_VORBIS_FLOAT}")

if(NOT BELL_DISABLE_SINKS)
    message(STATUS "    - ALSA sink: ${BELL_SINK_ALSA}")
    message(STATUS "    - PortAudio sink: ${BELL_SINK_PORTAUDIO}")
endif()

message(STATUS "    Use cJSON only: ${BELL_ONLY_CJSON}")
message(STATUS "    Disable Fmt: ${BELL_DISABLE_FMT}")
message(STATUS "    Disable Mqtt: ${BELL_DISABLE_MQTT}")
message(STATUS "    Disable Regex: ${BELL_DISABLE_REGEX}")
message(STATUS "    Disable Web server: ${BELL_DISABLE_WEBSERVER}")

# Include nanoPB library
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/external/nanopb/extra")
find_package(Nanopb REQUIRED)
message(${NANOPB_INCLUDE_DIRS})
list(APPEND EXTERNAL_INCLUDES ${NANOPB_INCLUDE_DIRS})

# CMake options
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED 20)
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(AUDIO_CODEC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/main/audio-codec")
set(AUDIO_CONTAINERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/main/audio-containers")
set(AUDIO_DSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/main/audio-dsp")
set(AUDIO_SINKS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/main/audio-sinks")
set(IO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/main/io")
set(PLATFORM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/main/platform")
set(UTILITIES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/main/utilities")

add_definitions("-DUSE_DEFAULT_STDLIB=1 -DTARGET_OS_IPHONE=0")

# Main library sources
file(GLOB SOURCES
    "external/nanopb/*.c"
    "main/utilities/*.cpp" "main/utilities/*.c"
    "main/io/*.cpp" "main/io/*.c"
)

list(APPEND EXTRA_INCLUDES "main/audio-codec/include")
list(APPEND EXTRA_INCLUDES "main/audio-dsp/include")
list(APPEND EXTRA_INCLUDES "main/audio-sinks/include")
list(APPEND EXTRA_INCLUDES "main/io/include")
list(APPEND EXTRA_INCLUDES "main/utilities/include")
list(APPEND EXTRA_INCLUDES "main/platform")

# Add platform specific sources
if(ESP_PLATFORM)
    file(GLOB ESP_PLATFORM_SOURCES "main/platform/esp/*.cpp" "main/platform/esp/*.c" "main/asm/biquad_f32_ae32.S")
    list(APPEND SOURCES ${ESP_PLATFORM_SOURCES})
endif()

if(APPLE)
    file(GLOB APPLE_PLATFORM_SOURCES "main/platform/apple/*.cpp" "main/platform/apple/*.c")
    list(APPEND SOURCES ${APPLE_PLATFORM_SOURCES})
    list(APPEND EXTERNAL_INCLUDES "/usr/local/opt/mbedtls@3/include")
endif()

if(UNIX AND NOT APPLE)
    file(GLOB LINUX_PLATFORM_SOURCES "main/platform/linux/*.cpp" "main/platform/linux/*.c")
    list(APPEND SOURCES ${LINUX_PLATFORM_SOURCES})
endif()

if(WIN32)
    file(GLOB WIN32_PLATFORM_SOURCES "main/platform/win32/*.cpp" "main/platform/win32/*.c")
    list(APPEND SOURCES ${WIN32_PLATFORM_SOURCES})
    list(APPEND EXTERNAL_INCLUDES "main/platform/win32")
endif()

# A hack to make Opus keep quiet
function(message)
    if(NOT MESSAGE_QUIET)
        _message(${ARGN})
    endif()
endfunction()


if(ESP_PLATFORM)
    if (IDF_VERSION_MAJOR LESS_EQUAL 4)
        list(APPEND EXTRA_LIBS idf::mdns idf::mbedtls idf::pthread idf::driver idf::lwip)
    else()	
        list(APPEND EXTRA_LIBS idf::espressif__mdns idf::mbedtls idf::pthread idf::driver idf::lwip)
    endif()
    add_definitions(-Wunused-const-variable -Wchar-subscripts -Wunused-label -Wmaybe-uninitialized -Wmisleading-indentation -Wno-stringop-overflow -Wno-error=format -Wno-format -Wno-stringop-overread -Wno-stringop-overflow)
else()
    find_package(Threads REQUIRED)
    find_package(MbedTLS REQUIRED)
    list(APPEND EXTERNAL_INCLUDES ${MBEDTLS_INCLUDE_DIRS})
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    list(APPEND EXTRA_LIBS ${MBEDTLS_LIBRARIES} Threads::Threads)

    if(MSVC)
        add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
        add_definitions(/wd4068 /wd4244 /wd4018 /wd4101 /wd4102 /wd4142)
    endif()
endif()

if (NOT BELL_DISABLE_MQTT)        
    file(GLOB MQTT_SOURCES "external/mqtt/*.c")
    list(APPEND SOURCES ${MQTT_SOURCES})
    list(APPEND EXTRA_INCLUDES "external/mqtt/include")
else()
    list(REMOVE_ITEM SOURCES "${IO_DIR}/BellMQTTClient.cpp")
endif()

if(NOT BELL_DISABLE_CODECS)
    file(GLOB EXTRA_SOURCES "main/audio-containers/*.cpp" "main/audio-codec/*.cpp" "main/audio-codec/*.c" "main/audio-dsp/*.cpp" "main/audio-dsp/*.c")

    list(APPEND SOURCES "${EXTRA_SOURCES}")
    list(APPEND SOURCES "${AUDIO_CODEC_DIR}/DecoderGlobals.cpp")
    list(APPEND SOURCES "${AUDIO_CODEC_DIR}/BaseCodec.cpp")
    list(APPEND SOURCES "${AUDIO_CODEC_DIR}/AudioCodecs.cpp")
    list(APPEND EXTRA_INCLUDES "main/audio-containers/include")

    # AAC-LC codec
    if(BELL_CODEC_AAC)
        add_subdirectory(external/opencore-aacdec)
        list(APPEND EXTRA_LIBS opencore-aacdec)
        list(APPEND SOURCES "${AUDIO_CODEC_DIR}/AACDecoder.cpp")
        list(APPEND CODEC_FLAGS "-DBELL_CODEC_AAC")
    endif()

    # MP3 codec
    if(BELL_CODEC_MP3)
        file(GLOB LIBHELIX_MP3_SOURCES "external/libhelix-mp3/*.c")
        list(APPEND LIBHELIX_SOURCES ${LIBHELIX_MP3_SOURCES})
        list(APPEND EXTERNAL_INCLUDES "external/libhelix-mp3")
        list(APPEND SOURCES "${AUDIO_CODEC_DIR}/MP3Decoder.cpp")
        list(APPEND CODEC_FLAGS "-DBELL_CODEC_MP3")
    endif()

    # MP3 codec
    # if(BELL_CODEC_ALAC)
    #     file(GLOB ALAC_SOURCES "external/alac/*.c" "external/alac/*.cpp")
    #     list(APPEND ALAC_SOURCES ${ALAC_SOURCES})
    #     list(APPEND EXTRA_INCLUDES "external/alac")

    #     # list(APPEND SOURCES "${AUDIO_DIR}/codec/ALACDecoder.cpp")
    #     list(APPEND CODEC_FLAGS "-DBELL_CODEC_ALAC")
    # endif()

    # libhelix Cygwin workaround
    if(CYGWIN)
        # Both Cygwin and ESP are Unix-like so this seems to work (or, at least, compile)
        set_source_files_properties("${AUDIO_CODEC_DIR}/DecoderGlobals.cpp" ${LIBHELIX_SOURCES} PROPERTIES COMPILE_FLAGS "-DESP_PLATFORM")
    endif()

    list(APPEND SOURCES ${LIBHELIX_SOURCES})
    list(APPEND SOURCES ${ALAC_SOURCES})

    # Vorbis codec
    if(BELL_CODEC_VORBIS)
        list(APPEND SOURCES "${AUDIO_CODEC_DIR}/VorbisDecoder.cpp")
        list(APPEND CODEC_FLAGS "-DBELL_CODEC_VORBIS")
    endif() 
    
    # Opus codec
    if(BELL_CODEC_OPUS)
        set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "")
        set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF)
        set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "")
        set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF)
        set(MESSAGE_QUIET ON)
        add_subdirectory("external/opus")
        unset(MESSAGE_QUIET)
        target_compile_options(opus PRIVATE "-O3")
        list(APPEND EXTRA_LIBS Opus::opus)
        list(APPEND SOURCES "${AUDIO_CODEC_DIR}/OPUSDecoder.cpp")
        list(APPEND CODEC_FLAGS -DBELL_CODEC_OPUS)
    endif()
    
    # Enable global codecs
    string(REPLACE ";" " " CODEC_FLAGS "${CODEC_FLAGS}")
    set_source_files_properties("${AUDIO_CODEC_DIR}/AudioCodecs.cpp" PROPERTIES COMPILE_FLAGS "${CODEC_FLAGS}")
else()  
    list(REMOVE_ITEM SOURCES "${IO_DIR}/EncodedAudioStream.cpp")
endif() 

if(NOT BELL_EXTERNAL_VORBIS STREQUAL "")
    message(STATUS "Using external Vorbis codec ${BELL_EXTERNAL_VORBIS}")
    list(APPEND EXTRA_LIBS ${BELL_EXTERNAL_VORBIS})
else()  
    file(GLOB TREMOR_SOURCES "external/tremor/*.c")
    list(REMOVE_ITEM TREMOR_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/external/tremor/ivorbisfile_example.c")
    list(APPEND SOURCES ${TREMOR_SOURCES})
    list(APPEND EXTERNAL_INCLUDES "external/tremor")
endif()

if(NOT BELL_DISABLE_SINKS)
    set(PLATFORM "unix")

    if(ESP_PLATFORM)
        set(PLATFORM "esp")
    endif()

    # Add all built-in audio sinks
    file(GLOB SINK_SOURCES "${AUDIO_SINKS_DIR}/${PLATFORM}/*.cpp" "${AUDIO_SINKS_DIR}/${PLATFORM}/*.c")
    list(APPEND EXTRA_INCLUDES "main/audio-sinks/include/${PLATFORM}")

    # Find ALSA if required, else remove the sink
    if(BELL_SINK_ALSA)
        find_package(ALSA REQUIRED)
        list(APPEND EXTERNAL_INCLUDES ${ALSA_INCLUDE_DIRS})
        list(APPEND EXTRA_LIBS ${ALSA_LIBRARIES})
    else()
        list(REMOVE_ITEM SINK_SOURCES "${AUDIO_SINKS_DIR}/unix/ALSAAudioSink.cpp")
    endif()

    # Find PortAudio if required, else remove the sink
    if(BELL_SINK_PORTAUDIO)
        find_package(Portaudio REQUIRED)
        list(APPEND EXTERNAL_INCLUDES ${PORTAUDIO_INCLUDE_DIRS})
        list(APPEND EXTRA_LIBS ${PORTAUDIO_LIBRARIES})
    else()
        list(REMOVE_ITEM SINK_SOURCES "${AUDIO_SINKS_DIR}/unix/PortAudioSink.cpp")
    endif()

    list(APPEND SOURCES ${SINK_SOURCES})
endif()

if(NOT BELL_ONLY_CJSON)
    set(JSON_SystemInclude ON CACHE INTERNAL "")
    add_subdirectory(external/nlohmann_json)
    list(APPEND EXTRA_LIBS nlohmann_json::nlohmann_json)
endif()	

if(BELL_EXTERNAL_CJSON)
	list(APPEND EXTRA_LIBS ${BELL_EXTERNAL_CJSON})
else()	
	list(APPEND SOURCES "external/cJSON/cJSON.c")
  list(APPEND EXTERNAL_INCLUDES "external/cJSON")
endif()	

if (NOT BELL_DISABLE_FMT)
  list(APPEND EXTERNAL_INCLUDES "external/fmt/include")
endif()

if(WIN32 OR UNIX)
    list(APPEND SOURCES "external/mdnssvc/mdns.c" "external/mdnssvc/mdnsd.c")
    list(APPEND EXTERNAL_INCLUDES "external/mdnssvc")
endif() 

if(NOT BELL_DISABLE_WEBSERVER)
    file(GLOB CIVET_SRC "external/civetweb/*.c" "external/civetweb/*.inl" "external/civetweb/*.cpp")
    list(APPEND SOURCES ${CIVET_SRC})
    list(APPEND EXTRA_INCLUDES "external/civetweb/include") 
else()
    list(REMOVE_ITEM SOURCES "${IO_DIR}/BellHTTPServer.cpp")    
    list(REMOVE_ITEM SOURCES "${IO_DIR}/MGStreamAdapter.cpp")    
endif()    

add_library(bell STATIC ${SOURCES})

# Add Apple Bonjour compatibility library for Linux
if(UNIX AND NOT APPLE)
    if (BELL_DISABLE_AVAHI)
        add_compile_definitions(BELL_DISABLE_AVAHI)
    else()
        list(APPEND EXTRA_LIBS avahi-client avahi-common)
    endif()
endif()

# PUBLIC to propagate esp-idf includes to bell dependents
target_link_libraries(bell PUBLIC ${EXTRA_LIBS})
target_include_directories(bell PUBLIC ${EXTRA_INCLUDES} ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(bell SYSTEM PUBLIC ${EXTERNAL_INCLUDES})
target_compile_definitions(bell PUBLIC PB_ENABLE_MALLOC FMT_HEADER_ONLY)

if(BELL_DISABLE_CODECS)
    target_compile_definitions(bell PUBLIC BELL_DISABLE_CODECS)
endif() 

if(BELL_VORBIS_FLOAT)
    target_compile_definitions(bell PUBLIC BELL_VORBIS_FLOAT)
endif() 

if(BELL_DISABLE_FMT)
	target_compile_definitions(bell PUBLIC BELL_DISABLE_FMT)
endif()

if(BELL_DISABLE_REGEX)
    target_compile_definitions(bell PUBLIC BELL_DISABLE_REGEX)
endif()	

if(BELL_ONLY_CJSON)
    target_compile_definitions(bell PUBLIC BELL_ONLY_CJSON)
endif()	

if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "SunOS")
    target_compile_definitions(bell PUBLIC PB_NO_STATIC_ASSERT)
endif()

