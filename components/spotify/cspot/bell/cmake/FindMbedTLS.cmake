if(BELL_EXTERNAL_MBEDTLS)
    set(MbedTLS_DIR ${BELL_EXTERNAL_MBEDTLS})
    message(STATUS "Using config mode, setting local mbedtls ${MbedTLS_DIR}")

	find_package(MbedTLS REQUIRED CONFIG)
	
	get_target_property(MBEDTLS_INCLUDE_DIRS MbedTLS::mbedtls INTERFACE_INCLUDE_DIRECTORIES)

    if(MSVC)
        set(MBEDTLS_RELEASE "RELEASE" CACHE STRING "local mbedtls version")
    else()
        set(MBEDTLS_RELEASE "NOCONFIG" CACHE STRING "local mbedtls version")
    endif()
    
    get_target_property(MBEDTLS_INFO MbedTLS::mbedtls IMPORTED_LOCATION_${MBEDTLS_RELEASE})
    set(MBEDTLS_LIBRARIES ${MBEDTLS_INFO})
    get_target_property(MBEDTLS_INFO MbedTLS::mbedx509 IMPORTED_LOCATION_${MBEDTLS_RELEASE})
    list(APPEND MBEDTLS_LIBRARIES ${MBEDTLS_INFO})
    get_target_property(MBEDTLS_INFO MbedTLS::mbedcrypto IMPORTED_LOCATION_${MBEDTLS_RELEASE})
    list(APPEND MBEDTLS_LIBRARIES ${MBEDTLS_INFO})
else()
	find_path(MBEDTLS_INCLUDE_DIRS mbedtls/ssl.h)

find_library(MBEDTLS_LIBRARY mbedtls)
find_library(MBEDX509_LIBRARY mbedx509)
find_library(MBEDCRYPTO_LIBRARY mbedcrypto)

set(MBEDTLS_LIBRARIES "${MBEDTLS_LIBRARY}" "${MBEDX509_LIBRARY}" "${MBEDCRYPTO_LIBRARY}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MbedTLS DEFAULT_MSG
    MBEDTLS_INCLUDE_DIRS MBEDTLS_LIBRARY MBEDX509_LIBRARY MBEDCRYPTO_LIBRARY)

	mark_as_advanced(MBEDTLS_INCLUDE_DIRS MBEDTLS_LIBRARY MBEDX509_LIBRARY MBEDCRYPTO_LIBRARY)
endif()