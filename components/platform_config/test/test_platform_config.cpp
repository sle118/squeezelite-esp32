
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "Config.h"
#include "DAC.pb.h"
#include "DAC_test_extra.pb.h"
#include "DAC_test_extra2.pb.h"
#include "Locking.h"
#include "PBW.h"
#include "State.pb.h"
#include "WifiList.h"
#include "configuration.pb.h"
#include "esp_log.h"
#include "test_common_init.h"
#include "tools.h"
#include "unity.h"

using namespace System;

// Helper macro to stringify the expanded value of a macro
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Use the helper macro to stringify LOG_LOCAL_LEVEL
#pragma message("The current log local level value is " TOSTRING(LOG_LOCAL_LEVEL))
#define AUTH_MODE_INDEX(i) ((start_auth_mode + i) % WIFI_AUTH_MAX == 0 ? (wifi_auth_mode_t)1 : (wifi_auth_mode_t)(start_auth_mode + i))
static const char* config_file_name = "settings.bin";

static const uint8_t bssid[6] = {0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56};
uint8_t fill_last = 0x56;
uint8_t start_rssi = 70;
uint8_t start_channel = 2;
auto start_auth_mode = WIFI_AUTH_WEP;
uint8_t fill_bssid[6] = {0xAB, 0xCD, 0xEF, 0x12, 0x34, fill_last};
static const char* char_bssid = "AB:CD:EF:12:34:56";
static const char* TAG = "test_platform_config";
const char* password = "TestPassword";
bool HasMemoryUsageIncreased(int round) {
    static const size_t thresholdInternal = 500; // Example threshold for internal memory
    static const size_t thresholdSPIRAM = 1000;  // Example threshold for SPI RAM
    size_t postTestFreeInternal = 0;
    size_t postTestFreeSPIRAM = 0;
    static size_t initialFreeInternal = 0;
    static size_t initialFreeSPIRAM = 0;
    auto minFreeInternal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    auto minFreeSPIRAM = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    if(round > 0) {
        postTestFreeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        postTestFreeSPIRAM = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        minFreeInternal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        minFreeSPIRAM = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);

        printf("Memory usage summary (after round %d): "
               "Internal: %zu->%zu bytes. Min free: %zu. Increase: %d bytes. "
               "SPIRAM: %zu->%zu bytes. Min free: %zu. Increase: %d bytes\n",
            round, initialFreeInternal, postTestFreeInternal, minFreeInternal, initialFreeInternal - postTestFreeInternal, initialFreeSPIRAM,
            postTestFreeSPIRAM, minFreeSPIRAM, initialFreeSPIRAM - postTestFreeSPIRAM);
        int32_t diffInternal = initialFreeInternal > postTestFreeInternal ? initialFreeInternal - postTestFreeInternal : 0;
        int32_t diffSPIRAM = initialFreeSPIRAM > postTestFreeSPIRAM ? initialFreeSPIRAM - postTestFreeSPIRAM : 0;
        if(diffSPIRAM > 0 || diffInternal > 0) {
            ESP_LOGW(TAG, "Internal increase: %d, SPIRAM: %d", diffInternal, diffSPIRAM);
            return true;
        }
    } else {
        initialFreeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        initialFreeSPIRAM = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        printf("Memory usage at start: "
               "Internal: %zu bytes. Min free: %zu. "
               "SPIRAM: %zu bytes. Min free: %zu.\n",
            initialFreeInternal, minFreeInternal, initialFreeSPIRAM, minFreeSPIRAM);
    }
    return false;
}
void advanceTime(int seconds) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tv.tv_sec += seconds;
    tv.tv_usec += seconds * 100;
    settimeofday((const timeval*)&tv, 0);
}
wifi_event_sta_connected_t getMockConnectedEvent(int i = 0) {
    wifi_event_sta_connected_t mock = {};
    mock.authmode = WIFI_AUTH_WPA3_PSK;
    memset(mock.ssid, 0x00, sizeof(mock.ssid));
    memset(mock.bssid, 0x00, sizeof(mock.bssid));
    snprintf((char*)mock.ssid, sizeof(mock.ssid), "SSID_%d", i);
    fill_bssid[5] = fill_last + i;
    memcpy(mock.bssid, fill_bssid, sizeof(mock.bssid));
    mock.channel = 6 + i;
    mock.ssid_len = strlen((char*)mock.ssid);
    return mock;
}
wifi_ap_record_t getMockAPRec(int i = 0) {
    wifi_ap_record_t mock = {};
    mock.primary = start_channel + i; // Set some channel
    mock.rssi = start_rssi + i;       // Set some RSSI value
    memset(mock.ssid, 0x00, sizeof(mock.ssid));
    memset(mock.bssid, 0x00, sizeof(mock.bssid));
    snprintf((char*)mock.ssid, sizeof(mock.ssid), "SSID_%d", i);
    fill_bssid[5] = fill_last + i;
    memcpy(mock.bssid, fill_bssid, sizeof(mock.bssid));
    mock.second = WIFI_SECOND_CHAN_ABOVE;
    mock.authmode = AUTH_MODE_INDEX(i);                  /**< authmode of AP */
    mock.pairwise_cipher = WIFI_CIPHER_TYPE_AES_GMAC128; /**< pairwise cipher of AP */
    mock.group_cipher = WIFI_CIPHER_TYPE_TKIP_CCMP;      /**< group cipher of AP */
    mock.ant = WIFI_ANT_ANT1;                            /**< antenna used to receive beacon from AP */
    mock.phy_11b = 1;                                    /**< bit: 0 flag to identify if 11b mode is enabled or not */
    mock.phy_11g = 0;                                    /**< bit: 1 flag to identify if 11g mode is enabled or not */
    mock.phy_11n = 1;                                    /**< bit: 2 flag to identify if 11n mode is enabled or not */
    mock.phy_lr = 0;                                     /**< bit: 3 flag to identify if low rate is enabled or not */
    mock.wps = 1;                                        /**< bit: 4 flag to identify if WPS is supported or not */
    mock.ftm_responder = 0;                              /**< bit: 5 flag to identify if FTM is supported in responder mode */
    mock.ftm_initiator = 1;                              /**< bit: 6 flag to identify if FTM is supported in initiator mode */
    return mock;
}
wifi_sta_config_t getMockSTA(int i = 0) {
    wifi_sta_config_t mock = {};
    memset(mock.ssid, 0x00, sizeof(mock.ssid));
    memset(mock.bssid, 0x00, sizeof(mock.bssid));
    snprintf((char*)mock.password, sizeof(mock.password), "Password_%d", i);
    snprintf((char*)mock.ssid, sizeof(mock.ssid), "SSID_%d", i);
    fill_bssid[5] = fill_last + i;
    memcpy(mock.bssid, fill_bssid, sizeof(mock.bssid));
    mock.channel = start_channel + i;
    mock.failure_retry_cnt = 2 + i;
    mock.listen_interval = 4 + i;
    mock.mbo_enabled = 1;
    mock.scan_method = WIFI_ALL_CHANNEL_SCAN;
    return mock;
}
sys_net_wifi_entry getMockEntry(int i = 0) {
    sys_net_wifi_entry mock = sys_net_wifi_entry_init_default;
    mock.auth_type = WifiList::GetAuthType(AUTH_MODE_INDEX(i));
    snprintf((char*)mock.password, sizeof(mock.password), "Password_%d", i);
    snprintf((char*)mock.ssid, sizeof(mock.ssid), "SSID_%d", i);
    fill_bssid[5] = fill_last + i;
    WifiList::FormatBSSID(mock.bssid, sizeof(mock.bssid), fill_bssid);
    mock.channel = start_channel + i;
    mock.connected = false;
    mock.has_last_seen = false;
    mock.has_last_try = false;
    mock.radio_type_count = 3;
    mock.radio_type = new sys_net_radio_types[mock.radio_type_count];
    mock.radio_type[0] = sys_net_radio_types_PHY_11B;
    mock.radio_type[1] = sys_net_radio_types_PHY_11G;
    mock.radio_type[2] = sys_net_radio_types_PHY_11N;
    mock.rssi = start_rssi + i;
    return mock;
}

void FillSSIDs(WifiList& manager, int count, int start = 1) {
    for(int i = start; i <= count; i++) {
        auto mock = getMockSTA(i);
        auto& entry = manager.AddUpdate(&mock);
        entry.rssi = 70 + i;
        entry.connected = true;
    }
}
void FillSSIDFromAPRec(WifiList& manager, int count, int start = 1) {
    for(int i = start; i <= count; i++) {
        auto mock = getMockAPRec(i);
        auto& entry = manager.AddUpdate(&mock);
        entry.rssi = 70 + i;
        entry.connected = true;
    }
}
void FillSSIDFromMockEntry(WifiList& manager, int count, int start = 1) {
    for(int i = start; i <= count; i++) {
        auto mock = getMockEntry(i);
        auto& entry = manager.AddUpdate(&mock);
        entry.rssi = 70 + i;
        entry.connected = true;
        WifiList::Release(mock);
    }
}
void eraseConfigFile() {
    PB<sys_config> wrapper(std::string("config"), &sys_config_msg, sizeof(sys_config_msg), false);
    erase_path(wrapper.GetFileName().c_str(), false);
}

TEST_CASE("Test empty target settings empty", "[platform_config]") {
    PB<sys_config> wrapper(std::string("Config"), &sys_config_msg, sizeof(sys_config_msg), false);
    sys_config* conf = wrapper.get();
    assert(conf != nullptr);
    conf->target = strdup_psram("");
#ifdef CONFIG_FW_PLATFORM_NAME
    system_set_string(&sys_config_msg, sys_config_target_tag, conf, CONFIG_FW_PLATFORM_NAME);
    TEST_ASSERT_TRUE(strcmp(conf->target, CONFIG_FW_PLATFORM_NAME) == 0);
#endif
}

TEST_CASE("Test init from default config", "[platform_config]") {
    struct stat fileInformation;
    PB<sys_config> wrapper(std::string("config"), &sys_config_msg, sizeof(sys_config_msg), false);
    sys_config* confprt = wrapper.get();
    auto filename = wrapper.GetFileName();
    erase_path(filename.c_str(), false);
    TEST_ASSERT_FALSE(get_file_info(&fileInformation, config_file_name));
    TEST_ASSERT_TRUE(strlen(STR_OR_BLANK(confprt->target)) == 0);
    TEST_ASSERT_FALSE(confprt->has_dev);
}
const sys_config* GetTestConfig() {
    static sys_config test_config;
    memset(&test_config, 0x00, sizeof(test_config));

    // Assuming test_config is an instance of sys_config or a similar structure
    test_config.has_dev = true;
    test_config.dev.has_spi = true;
    test_config.dev.spi.mosi = 4;
    test_config.dev.spi.clk = 5;
    test_config.dev.spi.dc = 18;
    test_config.dev.spi.host = sys_dev_common_hosts_Host1;

    test_config.dev.has_dac = true;
    test_config.dev.dac.bck = 25;
    test_config.dev.dac.ws = 26;
    test_config.dev.dac.dout = 33;
    test_config.dev.dac.model = sys_dac_models_WM8978;

    test_config.dev.has_display = true;
    test_config.dev.display.has_common = true;
    test_config.dev.display.common.width = 256;
    test_config.dev.display.common.height = 64;
    test_config.dev.display.common.HFlip = false;
    test_config.dev.display.common.VFlip = false;
    test_config.dev.display.common.rotate = false;
    test_config.dev.display.common.driver = sys_display_drivers_SSD1322;
    test_config.dev.display.common.reset = 21;
    test_config.dev.display.which_dispType = sys_display_config_spi_tag;
    test_config.dev.display.dispType.spi.cs = 19;
    test_config.dev.display.dispType.spi.speed = 8000000;
    test_config.dev.has_rotary = true;

    test_config.dev.rotary.A = 23;
    test_config.dev.rotary.B = 22;
    test_config.dev.rotary.SW = 34;

    test_config.dev.rotary.volume = true;
    test_config.dev.rotary.longpress = true;
    test_config.has_names = true;
    strcpy(test_config.names.device, "test_name");
    if(!test_config.target) { test_config.target = strdup_psram("test_target"); }
    return &test_config;
}
void check_sys_config_structure(sys_config* config) {
    auto check = GetTestConfig();
    // Test SPI configuration
    TEST_ASSERT_EQUAL(check->dev.has_spi, config->dev.has_spi);
    TEST_ASSERT_EQUAL(check->dev.spi.mosi, config->dev.spi.mosi);
    TEST_ASSERT_EQUAL(check->dev.spi.clk, config->dev.spi.clk);
    TEST_ASSERT_EQUAL(check->dev.spi.dc, config->dev.spi.dc);
    TEST_ASSERT_EQUAL(check->dev.spi.host, config->dev.spi.host);
    TEST_ASSERT_EQUAL(check->dev.has_dac, config->dev.has_dac);
    TEST_ASSERT_EQUAL(check->dev.dac.bck, config->dev.dac.bck);
    TEST_ASSERT_EQUAL(check->dev.dac.ws, config->dev.dac.ws);
    TEST_ASSERT_EQUAL(check->dev.dac.dout, config->dev.dac.dout);
    TEST_ASSERT_EQUAL(check->dev.dac.model, config->dev.dac.model);
    TEST_ASSERT_EQUAL(check->dev.has_display, config->dev.has_display);
    TEST_ASSERT_EQUAL(check->dev.display.common.width, config->dev.display.common.width);
    TEST_ASSERT_EQUAL(check->dev.display.common.height, config->dev.display.common.height);
    TEST_ASSERT_EQUAL(check->dev.display.common.HFlip, config->dev.display.common.HFlip);
    TEST_ASSERT_EQUAL(check->dev.display.common.VFlip, config->dev.display.common.VFlip);
    TEST_ASSERT_EQUAL(check->dev.display.common.rotate, config->dev.display.common.rotate);
    TEST_ASSERT_EQUAL(check->dev.display.common.driver, config->dev.display.common.driver);
    TEST_ASSERT_EQUAL(check->dev.display.common.reset, config->dev.display.common.reset);
    TEST_ASSERT_EQUAL(check->dev.display.which_dispType, config->dev.display.which_dispType);
    TEST_ASSERT_EQUAL(check->dev.display.dispType.spi.cs, config->dev.display.dispType.spi.cs);
    TEST_ASSERT_EQUAL(check->dev.display.dispType.spi.speed, config->dev.display.dispType.spi.speed);
    TEST_ASSERT_EQUAL(check->dev.has_rotary, config->dev.has_rotary);
    TEST_ASSERT_EQUAL(check->dev.rotary.A, config->dev.rotary.A);
    TEST_ASSERT_EQUAL(check->dev.rotary.B, config->dev.rotary.B);
    TEST_ASSERT_EQUAL(check->dev.rotary.SW, config->dev.rotary.SW);
    TEST_ASSERT_EQUAL(check->dev.rotary.volume, config->dev.rotary.volume);
    TEST_ASSERT_EQUAL(check->dev.rotary.longpress, config->dev.rotary.longpress);
    TEST_ASSERT_EQUAL_STRING(check->names.device, config->names.device);
    TEST_ASSERT_EQUAL_STRING(check->target, config->target);
}
TEST_CASE("Test change platform", "[platform_config]") {
    struct stat fileInformation;
    eraseConfigFile();
    std::stringstream test_target_file;
    test_target_file << spiffs_base_path << "/targets/" << GetTestConfig()->target << "/config.bin";
    PlatformConfig::ProtoWrapperHelper::CommitFile(test_target_file.str().c_str(), &sys_config_msg, GetTestConfig());

    // first ensure that the target state file exists.
    TEST_ASSERT_TRUE(get_file_info(&fileInformation, test_target_file.str().c_str()));
    TEST_ASSERT_TRUE(fileInformation.st_size > 0);
    platform = nullptr;

    // here we must use the configurator object
    // since we're testing some config_ functions
    std::string expectedTarget = "ESP32";
    TEST_ASSERT_NULL(platform);
    config_load();
    TEST_ASSERT_NOT_NULL(platform);
    TEST_ASSERT_NOT_NULL(platform->target)
    TEST_ASSERT_EQUAL_STRING(expectedTarget.c_str(), platform->target);
    config_set_target_reset(GetTestConfig()->target);
    check_sys_config_structure(platform);
    TEST_ASSERT_EQUAL_STRING(platform->target, GetTestConfig()->target);
    TEST_ASSERT_TRUE(erase_path(test_target_file.str().c_str(), false));
    TEST_ASSERT_FALSE(get_file_info(&fileInformation, test_target_file.str().c_str()));
}

// TEST_CASE("Test load state", "[platform_config]") {
//     eraseConfigFile();
//     const char* urlvalue = "http://somerandomurl";
//     config_load();
//     system_set_string(&sys_state_data_msg, sys_state_data_ota_url_tag, sys_state, urlvalue);
//     TEST_ASSERT_EQUAL_STRING(urlvalue, sys_state->ota_url);
//     ESP_LOGI(TAG, "Raising state change");
//     config_raise_state_changed();

// // create an async timer lambda to trigger the commit after 1 second so
// //we can test waitcommit
// config_commit_config

// TEST_CASE("Test Raise State Change", "[platform_config]") {
//     // config_load();
//     ESP_LOGI(TAG, "Raising state change");
//     TEST_ASSERT_FALSE(configurator.HasStateChanges());
//     TEST_ASSERT_FALSE(configurator.HasChanges());
//     config_raise_state_changed();
//     TEST_ASSERT_TRUE(configurator.HasStateChanges());
//     TEST_ASSERT_FALSE(configurator.HasChanges());
//     configurator.ResetStateModified();
//     TEST_ASSERT_FALSE(configurator.HasStateChanges());
//     TEST_ASSERT_FALSE(configurator.HasChanges());
// }
// TEST_CASE("Test Raise Change", "[platform_config]") {
//     // config_load();
//     ESP_LOGI(TAG, "Raising change");
//     PlatformConfig::PB wrapper =
//         PlatformConfig::PB("config", "", &sys_config_msg, Init_sys_config);
//     TEST_ASSERT_FALSE(configurator.HasStateChanges());
//     TEST_ASSERT_FALSE(configurator.HasChanges());
//     config_raise_changed();
//     TEST_ASSERT_TRUE(configurator.HasChanges());
//     TEST_ASSERT_FALSE(configurator.HasStateChanges());
//     configurator.ResetModified();
//     TEST_ASSERT_FALSE(configurator.HasChanges());
//     TEST_ASSERT_FALSE(configurator.HasStateChanges());
// }

TEST_CASE("Test Lock Unlock", "[platform_config]") {
    auto lock = PlatformConfig::Locking("test");
    TEST_ASSERT_FALSE(lock.IsLocked());
    TEST_ASSERT_TRUE(lock.Lock());
    TEST_ASSERT_TRUE(lock.Lock());
    TEST_ASSERT_TRUE(lock.IsLocked());
    lock.Unlock();
    TEST_ASSERT_TRUE(lock.IsLocked());
    lock.Unlock();
    TEST_ASSERT_FALSE(lock.IsLocked());
}

TEST_CASE("Recovery not updating message definition", "[platform_config]") {
    is_recovery_running = false;
    auto name = std::string("extra");
    struct stat struct_info_extra;
    struct stat struct_info_reco;
    // create instance with fresh definition
    erase_path(PBHelper::GetDefFileName(name).c_str(), false);
    PB<sys_dac_extra_config> wrapper_extra(name.c_str(), &sys_dac_extra_config_msg, sizeof(sys_dac_extra_config_msg));
    TEST_ASSERT_TRUE(get_file_info(&struct_info_extra, wrapper_extra.GetDefFileName().c_str()));
    TEST_ASSERT_TRUE(struct_info_extra.st_size > 0);
    auto& extra = wrapper_extra.Root();
    extra.dummy1 = 20;
    extra.dummy2 = 30;
    extra.has_dummy3 = true;
    extra.dummy3.level = sys_gpio_lvl_HIGH;
    extra.dummy3.pin = 22;
    wrapper_extra.CommitChanges();

    is_recovery_running = true;
    PB<sys_dac_config> wrapper(name, &sys_dac_config_msg, sizeof(sys_dac_config_msg));
    TEST_ASSERT_TRUE(get_file_info(&struct_info_reco, wrapper.GetDefFileName().c_str()));
    TEST_ASSERT_TRUE(struct_info_reco.st_size == struct_info_extra.st_size);
    TEST_ASSERT_EQUAL(wrapper_extra.GetDataSize(), wrapper.GetDataSize());
    wrapper.LoadFile(true);

    sys_dac_extra_config* config = reinterpret_cast<sys_dac_extra_config*>(wrapper.get());
    TEST_ASSERT_EQUAL(config->dummy1, extra.dummy1);
    TEST_ASSERT_EQUAL(config->dummy2, extra.dummy2);
    TEST_ASSERT_EQUAL(config->has_dummy3, extra.has_dummy3);
    TEST_ASSERT_EQUAL(config->dummy3.level, extra.dummy3.level);
    TEST_ASSERT_EQUAL(config->dummy3.pin, extra.dummy3.pin);

    config->bck = 55;
    wrapper.CommitChanges();
    PB<sys_dac_extra_config> check_structure(name.c_str(), &sys_dac_extra_config_msg, sizeof(sys_dac_extra_config_msg));
    check_structure.LoadFile(true);
    auto config_check = check_structure.get();
    TEST_ASSERT_EQUAL(config->bck, check_structure.get()->bck);
    TEST_ASSERT_EQUAL(config_check->dummy1, extra.dummy1);
    TEST_ASSERT_EQUAL(config_check->dummy2, extra.dummy2);
    TEST_ASSERT_EQUAL(config_check->has_dummy3, extra.has_dummy3);
    TEST_ASSERT_EQUAL(config_check->dummy3.level, extra.dummy3.level);
    TEST_ASSERT_EQUAL(config_check->dummy3.pin, extra.dummy3.pin);

    // not simulate an update
    is_recovery_running = false;
    PB<sys_dac_extra2_config> extra2(name.c_str(), &sys_dac_extra2_config_msg, sizeof(sys_dac_extra2_config_msg));
    extra2.LoadFile(true);
    auto config_extra2 = extra2.get();
    config_extra2->has_dummy4 = true;
    config_extra2->dummy4.pin = 99;
    extra2.CommitChanges();
    is_recovery_running = true;
    PB<sys_dac_config> wrapper2(name, &sys_dac_config_msg, sizeof(sys_dac_config_msg));

    wrapper2.LoadFile(true);
    wrapper2.Root().bck = 88;
    wrapper2.CommitChanges();

    is_recovery_running = false;
    extra2.LoadFile(true);
    TEST_ASSERT_EQUAL(config_extra2->bck, 88);
    TEST_ASSERT_TRUE(config_extra2->has_dummy4);
    TEST_ASSERT_EQUAL(config_extra2->dummy4.pin, 99);
}
TEST_CASE("String conversion from uint8_t array", "[WifiCredentialsManager]") {
    // Prepare test data
    const uint8_t testData[] = {'T', 'e', 's', 't', '\0', 'D', 'a', 't', 'a'};
    const size_t testDataLength = 9; // Including '\0'

    // Call the method
    std::string result = WifiList::toString(testData, testDataLength);

    // Assert expectations
    TEST_ASSERT_EQUAL_STRING_LEN("Test", result.c_str(), result.length());
}
TEST_CASE("Get SSID from wifi_sta_config_t", "[WifiCredentialsManager]") {
    // Prepare test data
    wifi_sta_config_t testConfig = getMockSTA(1);
    // Call the method
    std::string result = WifiList::GetSSID(&testConfig);
    TEST_ASSERT_EQUAL_STRING("SSID_1", result.c_str());
}
TEST_CASE("Get SSID from wifi_event_sta_connected_t", "[WifiCredentialsManager]") {
    wifi_event_sta_connected_t testEvent = {};
    const char* ssid = "EventSSID";
    memcpy(testEvent.ssid, ssid, strlen(ssid) + 1); // Including '\0'

    std::string result = WifiList::GetSSID(&testEvent);
    TEST_ASSERT_EQUAL_STRING(ssid, result.c_str());
}
TEST_CASE("Get SSID from wifi_ap_record_t", "[WifiCredentialsManager]") {
    wifi_ap_record_t testRecord = {};
    const char* ssid = "RecordSSID";
    memcpy(testRecord.ssid, ssid, strlen(ssid) + 1); // Including '\0'

    std::string result = WifiList::GetSSID(&testRecord);
    TEST_ASSERT_EQUAL_STRING(ssid, result.c_str());
}
TEST_CASE("Get Password from wifi_sta_config_t", "[WifiCredentialsManager]") {
    wifi_sta_config_t testConfig = {};
    memcpy(testConfig.password, password, strlen(password) + 1); // Including '\0'

    std::string result = WifiList::GetPassword(&testConfig);
    TEST_ASSERT_EQUAL_STRING(password, result.c_str());
}
TEST_CASE("Get BSSID from wifi_event_sta_connected_t", "[WifiCredentialsManager]") {
    wifi_event_sta_connected_t testEvent = {};
    memcpy(testEvent.bssid, bssid, sizeof(testEvent.bssid));
    std::string result = WifiCredentialsManager::GetBSSID(&testEvent);
    TEST_ASSERT_EQUAL_STRING(char_bssid, result.c_str());
}
TEST_CASE("Format BSSID from uint8_t array", "[WifiCredentialsManager]") {
    char buffer[18] = {0};
    WifiCredentialsManager::FormatBSSID(buffer, sizeof(buffer), bssid);
    TEST_ASSERT_EQUAL_STRING(char_bssid, buffer);
}

TEST_CASE("Update timestamp", "[WifiCredentialsManager]") {
    WifiCredentialsManager manager("test_manager");

    // Test with a non-null timestamp and an uninitialized flag
    google_protobuf_Timestamp ts = {0, 0};
    bool has_flag = false;

    bool result = manager.UpdateTimeStamp(&ts, has_flag);

    TEST_ASSERT_TRUE(result);             // Check if the method returns true for change
    TEST_ASSERT_TRUE(has_flag);           // Check if the flag is set to true
    TEST_ASSERT_NOT_EQUAL(0, ts.seconds); // Assuming gettimeofday() gives a non-zero time
    TEST_ASSERT_NOT_EQUAL(0, ts.nanos);

    // Store the updated values for comparison
    long prev_seconds = ts.seconds;
    long prev_nanos = ts.nanos;
    advanceTime(2);

    // Call the method again with the same timestamp
    result = manager.UpdateTimeStamp(&ts, has_flag);

    // Since the timestamp should be updated, check if the new values are different
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(has_flag);
    TEST_ASSERT_NOT_EQUAL(prev_seconds, ts.seconds);
    TEST_ASSERT_NOT_EQUAL(prev_nanos, ts.nanos);

    // Test with a null timestamp
    result = manager.UpdateTimeStamp(nullptr, has_flag);

    // The method should return false, and the flag should remain unchanged
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_TRUE(has_flag); // Flag remains unchanged
    advanceTime(-2);
}

TEST_CASE("Update wifi entry", "[WifiCredentialsManager]") {
    WifiCredentialsManager manager("test_manager");

    sys_net_wifi_entry existingEntry = getMockEntry();
    sys_net_wifi_entry updatedEntry = getMockEntry();

    // Modify updatedEntry with different values
    strcpy(updatedEntry.password, "NewPassword");
    updatedEntry.channel = 5;
    updatedEntry.auth_type = sys_net_auth_types_WPA2_PSK;
    delete[] updatedEntry.radio_type;
    updatedEntry.radio_type_count = 1;
    updatedEntry.radio_type = new sys_net_radio_types[updatedEntry.radio_type_count]; // Dynamic allocation for radio_type
    updatedEntry.radio_type[0] = sys_net_radio_types_PHY_11N;

    updatedEntry.rssi = 42;
    updatedEntry.connected = true;

    // Call the Update method
    bool result = manager.Update(existingEntry, updatedEntry);

    // Assert that the Update method returns true
    TEST_ASSERT_TRUE(result);

    // Assert that the existingEntry is updated correctly
    TEST_ASSERT_EQUAL_STRING("NewPassword", existingEntry.password);
    TEST_ASSERT_EQUAL(updatedEntry.channel, existingEntry.channel);
    TEST_ASSERT_EQUAL(sys_net_auth_types_WPA2_PSK, existingEntry.auth_type);
    TEST_ASSERT_EQUAL(sys_net_radio_types_PHY_11N, existingEntry.radio_type[0]);
    TEST_ASSERT_EQUAL(updatedEntry.radio_type_count, existingEntry.radio_type_count);
    TEST_ASSERT_EQUAL(updatedEntry.rssi, existingEntry.rssi);
    TEST_ASSERT_TRUE(existingEntry.connected);

    // Clean up dynamically allocated memory
    delete[] updatedEntry.radio_type;
    delete[] existingEntry.radio_type;
}

TEST_CASE("Update wifi entry from AP record", "[WifiCredentialsManager]") {
    WifiCredentialsManager manager("test_manager");

    // Create a mock wifi_ap_record_t
    wifi_ap_record_t mockAP = {};
    // Initialize mockAP with test data
    // ...

    // Add the initial entry
    sys_net_wifi_entry initialEntry = manager.ToSTAEntry(&mockAP);
    initialEntry.connected = false; // Initially not connected
    manager.AddUpdate(initialEntry);

    // Now call the update with the same AP but with 'connected' status
    bool result = manager.Update(&mockAP, true);

    // Assert that the update was successful
    TEST_ASSERT_TRUE(result);

    // Retrieve the entry and check if it's updated
    auto updatedEntry = manager.Get(&mockAP);
    TEST_ASSERT_NOT_NULL(updatedEntry);
    TEST_ASSERT_TRUE(updatedEntry->connected); // Check if connected is now true
    manager.Clear();
}

TEST_CASE("Format radio types", "[WifiCredentialsManager]") {
    WifiCredentialsManager manager("test_manager");

    // Create an array of radio types for testing
    sys_net_radio_types radioTypes[] = {sys_net_radio_types_PHY_11B, sys_net_radio_types_PHY_11G, sys_net_radio_types_PHY_11N, sys_net_radio_types_LR,
        sys_net_radio_types_WPS, sys_net_radio_types_FTM_RESPONDER, sys_net_radio_types_FTM_INITIATOR, sys_net_radio_types_UNKNOWN};
    pb_size_t count = sizeof(radioTypes) / sizeof(radioTypes[0]);

    // Call the formatRadioTypes method
    std::string formattedString = manager.formatRadioTypes(radioTypes, count);

    // Define the expected result
    std::string expectedResult = "B,G,N,L,W,FR,FI,U";

    // Assert that the formatted string is as expected
    TEST_ASSERT_EQUAL_STRING(expectedResult.c_str(), formattedString.c_str());
    manager.Clear();
}

TEST_CASE("Update wifi entry from STA config", "[WifiCredentialsManager]") {
    WifiCredentialsManager manager("test_manager");

    // Create a mock wifi_sta_config_t
    wifi_sta_config_t mockSTA = getMockSTA();

    // Create and add an initial entry
    sys_net_wifi_entry initialEntry = manager.ToSTAEntry(&mockSTA);
    initialEntry.connected = false; // Initially not connected
    manager.AddUpdate(initialEntry);

    // Modify the mockSTA for the update
    mockSTA.channel = 6; // Change the channel

    // Now call the update with the modified mockSTA and 'connected' status
    bool result = manager.Update(&mockSTA, true);

    // Assert that the update was successful
    TEST_ASSERT_TRUE(result);

    // Retrieve the updated entry and check if it's correctly updated
    auto updatedEntry = manager.Get(&mockSTA);
    TEST_ASSERT_NOT_NULL(updatedEntry);
    TEST_ASSERT_EQUAL(mockSTA.channel, updatedEntry->channel);
    TEST_ASSERT_TRUE(updatedEntry->connected); // Check if connected is now true
    manager.Clear();
}

TEST_CASE("Convert AP record to STA entry", "[WifiCredentialsManager]") {
    int testindex = 1;
    WifiCredentialsManager manager("test_manager");

    // Create a mock wifi_ap_record_t
    wifi_ap_record_t mockAP = getMockAPRec(testindex);

    sys_net_wifi_entry entry; // Create an entry to be populated

    // Call ToSTAEntry
    sys_net_wifi_entry& resultEntry = manager.ToSTAEntry(&mockAP, entry);

    // Assert that the entry is populated correctly
    TEST_ASSERT_EQUAL_STRING("SSID_1", resultEntry.ssid);
    TEST_ASSERT_EQUAL(start_channel + testindex, resultEntry.channel);
    TEST_ASSERT_EQUAL(start_rssi + testindex, resultEntry.rssi);
    TEST_ASSERT_EQUAL(WifiList::GetAuthType(AUTH_MODE_INDEX(testindex)), resultEntry.auth_type);
    TEST_ASSERT_EQUAL(4, resultEntry.radio_type_count);
    std::string formattedString = manager.formatRadioTypes(resultEntry.radio_type, resultEntry.radio_type_count);
    // Define the expected result
    std::string expectedResult = "B,N,W,FI";
    TEST_ASSERT_EQUAL_STRING(expectedResult.c_str(), formattedString.c_str());
    char bssid_buffer[20] = {};
    WifiList::FormatBSSID(bssid_buffer, sizeof(bssid_buffer), mockAP.bssid);
    TEST_ASSERT_EQUAL_STRING((char*)bssid_buffer, resultEntry.bssid);

    manager.Clear();
}

TEST_CASE("Remove entries from manager instance", "[WifiCredentialsManager]") {
    WifiCredentialsManager manager("test_manager");
    FillSSIDs(manager, 4);
    TEST_ASSERT_EQUAL(4, manager.GetCount());
    TEST_ASSERT_TRUE(manager.RemoveCredential("SSID_1"));
    TEST_ASSERT_EQUAL(3, manager.GetCount());
    TEST_ASSERT_FALSE(manager.RemoveCredential("SSID_1"));
    TEST_ASSERT_EQUAL(3, manager.GetCount());
    manager.Clear();
    TEST_ASSERT_EQUAL(0, manager.GetCount());

    FillSSIDs(manager, 4);
    TEST_ASSERT_EQUAL(4, manager.GetCount());
    TEST_ASSERT_TRUE(manager.RemoveCredential(std::string("SSID_1")));
    TEST_ASSERT_FALSE(manager.RemoveCredential(std::string("SSID_1")));
    TEST_ASSERT_TRUE(manager.RemoveCredential("SSID_2"));
    TEST_ASSERT_FALSE(manager.RemoveCredential(std::string("SSID_2")));
    TEST_ASSERT_EQUAL(2, manager.GetCount());
    manager.Clear();
    TEST_ASSERT_EQUAL(0, manager.GetCount());
}
TEST_CASE("Reset all entries", "[WifiCredentialsManager]") {
    WifiCredentialsManager manager("test_manager");
    FillSSIDs(manager, 4);
    for(auto& e : manager) {
        TEST_ASSERT_TRUE(e.second.rssi > 0);
        TEST_ASSERT_TRUE(e.second.connected);
    }
    manager.ResetRSSI();
    for(auto& e : manager) {
        TEST_ASSERT_TRUE(e.second.rssi == 0);
        TEST_ASSERT_TRUE(e.second.connected);
    }
    manager.Clear();
    FillSSIDs(manager, 4);
    manager.ResetConnected();
    for(auto& e : manager) {
        TEST_ASSERT_TRUE(e.second.rssi > 0);
        TEST_ASSERT_FALSE(e.second.connected);
    }
}

TEST_CASE("Getting/setting Connected", "[WifiCredentialsManager]") {
    WifiCredentialsManager manager("test_manager");
    auto conn_mock_4 = getMockConnectedEvent(4);
    auto conn_mock_5 = getMockConnectedEvent(5);
    auto entry_5 = getMockEntry(5);
    char bssid_buffer[20] = {};
    FillSSIDs(manager, 4);
    for(auto& e : manager) {
        if(strcmp(e.second.ssid, "SSID_3") != 0) { e.second.connected = false; }
    }
    auto entry = manager.GetConnected();
    TEST_ASSERT_EQUAL_STRING("SSID_3", entry->ssid);
    TEST_ASSERT_FALSE(manager.SetConnected(&conn_mock_5, true));
    manager.AddUpdate(&entry_5);
    WifiCredentialsManager::Release(entry_5);
    TEST_ASSERT_TRUE(manager.SetConnected(&conn_mock_5, true));
    entry = manager.GetConnected();
    TEST_ASSERT_EQUAL_STRING_LEN((char*)conn_mock_5.ssid, entry->ssid, conn_mock_5.ssid_len);
    WifiCredentialsManager::FormatBSSID(bssid_buffer, sizeof(bssid_buffer), conn_mock_5.bssid);
    TEST_ASSERT_EQUAL_STRING((char*)bssid_buffer, entry->bssid);
    TEST_ASSERT_EQUAL(WifiList::GetAuthType(conn_mock_5.authmode), entry->auth_type);
    TEST_ASSERT_EQUAL(conn_mock_5.channel, entry->channel);
    TEST_ASSERT_TRUE(manager.SetConnected(&conn_mock_4, true));
    entry = manager.GetConnected();
    TEST_ASSERT_EQUAL_STRING_LEN((char*)conn_mock_4.ssid, entry->ssid, conn_mock_5.ssid_len);
    WifiList::FormatBSSID(bssid_buffer, sizeof(bssid_buffer), conn_mock_4.bssid);
    TEST_ASSERT_EQUAL_STRING((char*)bssid_buffer, entry->bssid);
    TEST_ASSERT_EQUAL(WifiList::GetAuthType(conn_mock_4.authmode), entry->auth_type);
    TEST_ASSERT_EQUAL(conn_mock_4.channel, entry->channel);
    manager.ResetConnected();
    TEST_ASSERT_NULL(manager.GetConnected());
    manager.Clear();
}

TEST_CASE("Getting by index/by name", "[WifiCredentialsManager]") {
    char buffer[25] = {};
    WifiCredentialsManager manager("test_manager");
    FillSSIDs(manager, 4);
    for(int i = 0; i < manager.GetCount(); i++) {
        sprintf(buffer, "SSID_%d", i + 1);
        auto mockap = getMockAPRec(i + 1);
        auto mockSTA = getMockSTA(i + 1);
        auto entry = manager.GetIndex(i);
        TEST_ASSERT_EQUAL_STRING(buffer, entry->ssid);
        auto pEntry = manager.Get(std::string(buffer));
        TEST_ASSERT_EQUAL_STRING(buffer, pEntry->ssid);
        pEntry = manager.Get(buffer);
        TEST_ASSERT_EQUAL_STRING(buffer, pEntry->ssid);
        pEntry = manager.Get(&mockap);
        TEST_ASSERT_EQUAL_STRING(buffer, pEntry->ssid);
        pEntry = manager.Get(&mockSTA);
        TEST_ASSERT_EQUAL_STRING(buffer, pEntry->ssid);
    }
    auto entry = manager.GetStrongestSTA();
    TEST_ASSERT_EQUAL_STRING("SSID_4", entry.ssid);
    manager.Clear();
}

TEST_CASE("Update last try", "[WifiCredentialsManager]") {
    char buffer[25] = {};
    google_protobuf_Timestamp ts;
    bool flag;
    WifiCredentialsManager::UpdateTimeStamp(&ts, flag);

    // now advance the time to ensure tests are a success
    advanceTime(2);

    WifiList manager("test_manager");
    FillSSIDs(manager, 4);
    for(int i = 0; i < manager.GetCount(); i++) {
        sprintf(buffer, "SSID_%d", i + 1);
        auto mockap = getMockAPRec(i + 1);
        auto mockSTA = getMockSTA(i + 1);
        auto entry = manager.GetIndex(i);
        entry->has_last_try = false;
        memset(&entry->last_try, 0x00, sizeof(entry->last_try));
        manager.UpdateLastTry(entry);
        TEST_ASSERT_TRUE(entry->has_last_try);
        TEST_ASSERT_TRUE(entry->last_try.seconds >= ts.seconds);
        entry->has_last_try = false;
        memset(&entry->last_try, 0x00, sizeof(entry->last_try));
        manager.UpdateLastTry(std::string(buffer));
        TEST_ASSERT_TRUE(entry->has_last_try);
        TEST_ASSERT_TRUE(entry->last_try.seconds >= ts.seconds);
        entry->has_last_try = false;
        memset(&entry->last_try, 0x00, sizeof(entry->last_try));
        manager.UpdateLastTry(&mockap);
        TEST_ASSERT_TRUE(entry->has_last_try);
        TEST_ASSERT_TRUE(entry->last_try.seconds >= ts.seconds);
        entry->has_last_try = false;
        memset(&entry->last_try, 0x00, sizeof(entry->last_try));
        manager.UpdateLastTry(&mockSTA);
        TEST_ASSERT_TRUE(entry->has_last_try);
        TEST_ASSERT_TRUE(entry->last_try.seconds >= ts.seconds);
    }
    auto entry = manager.GetStrongestSTA();
    TEST_ASSERT_EQUAL_STRING("SSID_4", entry.ssid);
    manager.Clear();
    advanceTime(-2);
}

TEST_CASE("Update last seen", "[WifiCredentialsManager]") {
    char buffer[25] = {};
    google_protobuf_Timestamp ts;
    bool flag;
    WifiCredentialsManager::UpdateTimeStamp(&ts, flag);

    // now advance the time to ensure tests are a success
    advanceTime(2);

    WifiList manager("test_manager");
    FillSSIDs(manager, 4);
    for(int i = 0; i < manager.GetCount(); i++) {
        sprintf(buffer, "SSID_%d", i + 1);
        auto mockap = getMockAPRec(i + 1);
        auto mockSTA = getMockSTA(i + 1);
        auto entry = manager.GetIndex(i);
        entry->has_last_seen = false;
        memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
        manager.UpdateLastSeen(entry);
        TEST_ASSERT_TRUE(entry->has_last_seen);
        TEST_ASSERT_TRUE(entry->last_seen.seconds >= ts.seconds);
        entry->has_last_seen = false;
        memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
        manager.UpdateLastSeen(std::string(buffer));
        TEST_ASSERT_TRUE(entry->has_last_seen);
        TEST_ASSERT_TRUE(entry->last_seen.seconds >= ts.seconds);
        entry->has_last_seen = false;
        memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
        manager.UpdateLastSeen(&mockap);
        TEST_ASSERT_TRUE(entry->has_last_seen);
        TEST_ASSERT_TRUE(entry->last_seen.seconds >= ts.seconds);
        entry->has_last_seen = false;
        memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
        manager.UpdateLastSeen(&mockSTA);
        TEST_ASSERT_TRUE(entry->has_last_seen);
        TEST_ASSERT_TRUE(entry->last_seen.seconds >= ts.seconds);
    }

    manager.Clear();
    advanceTime(-2);
}

TEST_CASE("Memory leak test", "[WifiCredentialsManager]") {
    char buffer[25] = {};
    char err_buffer[55] = {};
    int fillqty = 20;
    google_protobuf_Timestamp ts;
    bool flag;

    WifiCredentialsManager::UpdateTimeStamp(&ts, flag);

    // now advance the time to ensure tests are a success
    advanceTime(2);
    HasMemoryUsageIncreased(0);

    for(int runs = 0; runs < 100; runs++) {
        WifiCredentialsManager manager("test_manager");
        FillSSIDs(manager, fillqty);
        for(int i = 1; i <= manager.GetCount(); i++) {
            sprintf(buffer, "SSID_%d", i);
            sprintf(err_buffer, "Round %d, SSID_%d", runs, i);
            auto mockap = getMockAPRec(i);
            auto mockSTA = getMockSTA(i);
            auto entry = manager.Get(buffer);
            TEST_ASSERT_EQUAL_STRING(buffer, entry->ssid);
            entry->has_last_seen = false;
            memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
            manager.UpdateLastSeen(entry);
            TEST_ASSERT_TRUE(entry->has_last_seen);
            TEST_ASSERT_TRUE(entry->last_seen.seconds >= ts.seconds);
            entry->has_last_seen = false;
            memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
            manager.UpdateLastSeen(std::string(buffer));
            TEST_ASSERT_TRUE_MESSAGE(entry->has_last_seen, err_buffer);
            TEST_ASSERT_TRUE_MESSAGE(entry->last_seen.seconds >= ts.seconds, err_buffer);
            entry->has_last_seen = false;
            memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
            manager.UpdateLastSeen(&mockap);
            TEST_ASSERT_TRUE_MESSAGE(entry->has_last_seen, err_buffer);
            TEST_ASSERT_TRUE_MESSAGE(entry->last_seen.seconds >= ts.seconds, err_buffer);
            entry->has_last_seen = false;
            memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
            manager.UpdateLastSeen(&mockSTA);
            TEST_ASSERT_TRUE_MESSAGE(entry->has_last_seen, err_buffer);
            TEST_ASSERT_TRUE_MESSAGE(entry->last_seen.seconds >= ts.seconds, err_buffer);
        }
        manager.Clear();
    }
    TEST_ASSERT_FALSE(HasMemoryUsageIncreased(1));

    for(int runs = 0; runs < 100; runs++) {
        WifiCredentialsManager manager("test_manager");
        FillSSIDFromAPRec(manager, fillqty);
        for(int i = 1; i <= manager.GetCount(); i++) {
            sprintf(buffer, "SSID_%d", i);
            sprintf(err_buffer, "Round %d, SSID_%d", runs, i);
            auto mockap = getMockAPRec(i);
            auto mockSTA = getMockSTA(i);
            auto entry = manager.GetIndex(i - 1);
            entry->has_last_seen = false;
            memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
            manager.UpdateLastSeen(entry);
            TEST_ASSERT_TRUE_MESSAGE(entry->has_last_seen, err_buffer);
            TEST_ASSERT_TRUE_MESSAGE(entry->last_seen.seconds >= ts.seconds, err_buffer);
            entry->has_last_seen = false;
            memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
            manager.UpdateLastSeen(std::string(buffer));
            TEST_ASSERT_TRUE_MESSAGE(entry->has_last_seen, err_buffer);
            TEST_ASSERT_TRUE_MESSAGE(entry->last_seen.seconds >= ts.seconds, err_buffer);
            entry->has_last_seen = false;
            memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
            manager.UpdateLastSeen(&mockap);
            TEST_ASSERT_TRUE_MESSAGE(entry->has_last_seen, err_buffer);
            TEST_ASSERT_TRUE_MESSAGE(entry->last_seen.seconds >= ts.seconds, err_buffer);
            entry->has_last_seen = false;
            memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
            manager.UpdateLastSeen(&mockSTA);
            TEST_ASSERT_TRUE_MESSAGE(entry->has_last_seen, err_buffer);
            TEST_ASSERT_TRUE_MESSAGE(entry->last_seen.seconds >= ts.seconds, err_buffer);
        }
        manager.Clear();
    }
    TEST_ASSERT_FALSE(HasMemoryUsageIncreased(2));

    for(int runs = 0; runs < 100; runs++) {
        WifiCredentialsManager manager("test_manager");
        FillSSIDFromMockEntry(manager, fillqty);
        for(int i = 1; i <= manager.GetCount(); i++) {
            sprintf(buffer, "SSID_%d", i);
            sprintf(err_buffer, "Round %d, SSID_%d", runs, i);
            auto mockap = getMockAPRec(i);
            auto mockSTA = getMockSTA(i);
            auto entry = manager.GetIndex(i - 1);
            entry->has_last_seen = false;
            memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
            manager.UpdateLastSeen(entry);
            TEST_ASSERT_TRUE_MESSAGE(entry->has_last_seen, err_buffer);
            TEST_ASSERT_TRUE_MESSAGE(entry->last_seen.seconds >= ts.seconds, err_buffer);
            entry->has_last_seen = false;
            memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
            manager.UpdateLastSeen(std::string(buffer));
            TEST_ASSERT_TRUE_MESSAGE(entry->has_last_seen, err_buffer);
            TEST_ASSERT_TRUE_MESSAGE(entry->last_seen.seconds >= ts.seconds, err_buffer);
            entry->has_last_seen = false;
            memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
            manager.UpdateLastSeen(&mockap);
            TEST_ASSERT_TRUE_MESSAGE(entry->has_last_seen, err_buffer);
            TEST_ASSERT_TRUE_MESSAGE(entry->last_seen.seconds >= ts.seconds, err_buffer);
            entry->has_last_seen = false;
            memset(&entry->last_seen, 0x00, sizeof(entry->last_seen));
            manager.UpdateLastSeen(&mockSTA);
            TEST_ASSERT_TRUE_MESSAGE(entry->has_last_seen, err_buffer);
            TEST_ASSERT_TRUE_MESSAGE(entry->last_seen.seconds >= ts.seconds, err_buffer);
        }
        manager.Clear();
    }
    advanceTime(-2);
    TEST_ASSERT_FALSE(HasMemoryUsageIncreased(3));
}