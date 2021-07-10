idf_component_register(SRC_DIRS .  
						PRIV_REQUIRES esp_common wifi-manager pthread squeezelite-ota platform_console telnet display led_strip
                    	INCLUDE_DIRS .
                    	EMBED_FILES ../server_certs/github.pem
                    	)
#get_target_property(ill ${COMPONENT_LIB} INTERFACE_LINK_LIBRARIES)
#message("${COMPONENT_LIB} INTERFACE_LINK_LIBRARIES = ${ill}")

