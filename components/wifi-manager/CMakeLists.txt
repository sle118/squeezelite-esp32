set(CMAKE_CXX_STANDARD 20)

set( WEBPACK_DIR webapp/webpack/dist )

# for the forgetful, REQUIRES cannot use CONFIG_XXX due to parsing order
if(IDF_TARGET STREQUAL "esp32")
    set(target_requires "driver_bt")
endif()
 
idf_component_register( SRC_DIRS .  UML-State-Machine-in-C/src
						INCLUDE_DIRS . UML-State-Machine-in-C/src
						REQUIRES squeezelite-ota json mdns 
						PRIV_REQUIRES tools services platform_config esp_common json newlib freertos spi_flash nvs_flash mdns pthread wpa_supplicant platform_console esp_http_server console ${target_requires}
)

# Define the source and destination directories
set(WEBAPP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/webapp/dist")
set(WEBAPP_DEST_DIR "${CMAKE_SOURCE_DIR}/spiffs/www")

# Collect all .gz and .png files in the source directory
file(GLOB_RECURSE WEBAPP_FILES 
     "${WEBAPP_SOURCE_DIR}/*.gz"
     "${WEBAPP_SOURCE_DIR}/*.png")

	 
# # Custom command to copy files, preserving the directory structure
# foreach(FILE_PATH ${WEBAPP_FILES})
# # Get the relative path from the source directory
# 	file(RELATIVE_PATH REL_PATH "${WEBAPP_SOURCE_DIR}" "${FILE_PATH}")
# 	# Compute the destination file path
# 	set(DEST_PATH "${WEBAPP_DEST_DIR}/${REL_PATH}")

# 	# Create a custom command to copy each file
# 	add_custom_command(
# 		OUTPUT "${DEST_PATH}"
# 		COMMAND ${CMAKE_COMMAND} -E copy "${FILE_PATH}" "${DEST_PATH}"
# 		DEPENDS "${FILE_PATH}"
# 	)
# 	# Add the destination path to a list of outputs
# 	list(APPEND WEBAPP_OUTPUT_FILES "${DEST_PATH}")
# endforeach()

# # Custom target that depends on all copied files
# add_custom_target(copy_webapp DEPENDS ${WEBAPP_OUTPUT_FILES} )

# # Ensure the destination directory exists
# add_custom_command(TARGET copy_webapp PRE_BUILD COMMAND ${CMAKE_COMMAND} -E make_directory "${WEBAPP_DEST_DIR}" )

# add_dependencies(${COMPONENT_LIB} copy_webapp)