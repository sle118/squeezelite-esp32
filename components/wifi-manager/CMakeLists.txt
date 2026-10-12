set( WEBPACK_DIR webapp/webpack/dist )

# for the forgetful, REQUIRES cannot use CONFIG_XXX due to parsing order
if(IDF_TARGET STREQUAL "esp32")
    set(target_requires "driver_bt")
endif()

idf_component_register( SRC_DIRS . webapp UML-State-Machine-in-C/src
						INCLUDE_DIRS . webapp UML-State-Machine-in-C/src
						REQUIRES squeezelite-ota json mdns 
						PRIV_REQUIRES tools services platform_config esp_common json newlib freertos spi_flash nvs_flash mdns pthread wpa_supplicant platform_console esp_http_server console squeezelite ${target_requires}
)

include(webapp/webapp.cmake)