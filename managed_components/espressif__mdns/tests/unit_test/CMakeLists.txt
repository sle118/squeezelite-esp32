# This is the project CMakeLists.txt file for the test subproject
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "6.0")
    set(test_component_dir $ENV{IDF_PATH}/tools/test_apps/components)
else()
    set(test_component_dir $ENV{IDF_PATH}/tools/unit-test-app/components)
endif()

set(EXTRA_COMPONENT_DIRS ../..
                         ${test_component_dir})

project(mdns_test)
