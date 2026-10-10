idf_component_register(SRC_DIRS .
					  INCLUDE_DIRS .
					  REQUIRES app_update esp_https_ota esp_http_client mqtt json mdns
					  PRIV_REQUIRES  console tools display led_strip services platform_config spi_flash vfs console freertos platform_console nvs_flash
					  )
