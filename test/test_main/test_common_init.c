#include "unity.h"
#include "tools.h"        // Assuming tools.h contains init_spiffs and listFiles

#include "test_common_init.h"
#include "tools_spiffs_utils.h"
static const char * TAG = "test_common";

esp_err_t start_ota_return_code=ESP_OK;
void start_ota_set_return(esp_err_t mockreturn){
    start_ota_return_code = mockreturn;

}
esp_err_t start_ota(const char * bin_url, char * bin_buffer, uint32_t length) {
    ESP_LOGI(TAG,"Received OTA Request url %s/bin buffer size: %d. returning: %s",bin_url,length,esp_err_to_name(start_ota_return_code));
    return start_ota_return_code;
}
esp_log_level_t SetLogLevels(esp_log_level_t level) {
    esp_log_level_set("*", level);
    return esp_log_level_get(TAG);
}
TEST_CASE("Raise Log Level","[test]") {
    TEST_ASSERT_TRUE(SetLogLevels(ESP_LOG_DEBUG) == ESP_LOG_DEBUG );
}
TEST_CASE("Lower Log Level","[test]") {
    TEST_ASSERT_TRUE(SetLogLevels(ESP_LOG_INFO) == ESP_LOG_INFO );
}

TEST_CASE("List dir content", "[tools]") {
    common_test_init();
    listFiles("/spiffs");
    TEST_ASSERT_TRUE(true);
}