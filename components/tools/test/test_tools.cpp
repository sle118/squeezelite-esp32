#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include "test_common_init.h"
#include "tools.h"
#include "unity.h"
#include <sstream>
#include <string>
#include <vector>
#include "tools_spiffs_utils.h"

static const char* TAG = "test_tools";

static const char* data = "SomeTestData\0";
#define UINT8T_DATA (const uint8_t*)data
static size_t len = strlen(data);

TEST_CASE("Test Write/Read File", "[tools]") {
    common_test_init();
    const char* testfilename = "/spiffs/test_file.bin";
    size_t loaded = 0;
    // allow +1 to the length below for null termination of the string
    TEST_ASSERT_TRUE(write_file(UINT8T_DATA, len + 1, testfilename));
    char* loaded_data = (char*)load_file_psram(&loaded, testfilename);
    TEST_ASSERT_EQUAL(loaded, len + 1);
    TEST_ASSERT_NOT_EQUAL(loaded_data, NULL);
    TEST_ASSERT_EQUAL_STRING((const char*)data, (char*)loaded_data);
    free(loaded_data);
}

TEST_CASE("Test Simple Erase File ", "[tools]") {
    std::ostringstream fullfilename;
    const char* prefix = "test_file";

    struct stat fileInfo;
    // test for simple file names
    fullfilename << "/spiffs/" << prefix << ".bin";
    TEST_ASSERT_TRUE(write_file(UINT8T_DATA, len, fullfilename.str().c_str()));
    TEST_ASSERT_TRUE(get_file_info(&fileInfo, fullfilename.str().c_str()));
    TEST_ASSERT_EQUAL(fileInfo.st_size, len);
    TEST_ASSERT_TRUE(erase_path(fullfilename.str().c_str(), false));
    TEST_ASSERT_FALSE(get_file_info(&fileInfo, fullfilename.str().c_str()));
    fullfilename.clear();
    fullfilename.str("");
}
TEST_CASE("Test Wildcard Erase File ", "[tools]") {
    std::ostringstream fullfilename;
    std::ostringstream fullfilename2;
    std::ostringstream fullnamewc;
    const char* prefix = "/spiffs/test_file";
    struct stat fileInfo;
    // Test for wildcard

    fullfilename << prefix << ".bin";
    fullfilename2 << prefix << ".bin2";
    fullnamewc << prefix << "*.bin";
    TEST_ASSERT_TRUE(write_file(UINT8T_DATA, len, fullfilename.str().c_str()));
    TEST_ASSERT_TRUE(write_file(UINT8T_DATA, len, fullfilename2.str().c_str()));
    TEST_ASSERT_TRUE(get_file_info(&fileInfo, fullfilename.str().c_str()));
    TEST_ASSERT_EQUAL(fileInfo.st_size, len);
    TEST_ASSERT_TRUE(erase_path(fullnamewc.str().c_str(), false));
    TEST_ASSERT_FALSE(get_file_info(&fileInfo, fullfilename.str().c_str()));
    TEST_ASSERT_TRUE(get_file_info(&fileInfo, fullfilename2.str().c_str()));
    fullnamewc.str("");
    fullnamewc << prefix << ".bin*";
    TEST_ASSERT_TRUE(erase_path(fullnamewc.str().c_str(), false));
    TEST_ASSERT_FALSE(get_file_info(&fileInfo, fullfilename2.str().c_str()));
}
TEST_CASE("Test Folder Erase File ", "[tools]") {
    std::ostringstream fullfilename;
    const char* prefix = "test_file";
    struct stat fileInfo;
    fullfilename << "/spiffs/somefolder/" << prefix << ".bin";
    TEST_ASSERT_TRUE(write_file(UINT8T_DATA, len, fullfilename.str().c_str()));
    TEST_ASSERT_TRUE(get_file_info(&fileInfo, fullfilename.str().c_str()));
    TEST_ASSERT_EQUAL(fileInfo.st_size, len);
    TEST_ASSERT_TRUE(erase_path(fullfilename.str().c_str(), false));
    TEST_ASSERT_FALSE(get_file_info(&fileInfo, fullfilename.str().c_str()));
}
TEST_CASE("Test Folder Wildcard Erase File ", "[tools]") {
    std::ostringstream fullfilename;
    std::ostringstream fullnamewc;
    const char* prefix = "test_file";
    struct stat fileInfo;

    fullfilename << "/spiffs/somefolder/" << prefix << ".bin";
    fullnamewc << prefix << "*.bin";
    TEST_ASSERT_TRUE(write_file(UINT8T_DATA, len, fullfilename.str().c_str()));
    TEST_ASSERT_TRUE(get_file_info(&fileInfo, fullfilename.str().c_str()));
    TEST_ASSERT_EQUAL(fileInfo.st_size, len);
    TEST_ASSERT_TRUE(erase_path(fullfilename.str().c_str(), false));
    TEST_ASSERT_FALSE(get_file_info(&fileInfo, fullnamewc.str().c_str()));
}

const std::vector<std::string> testRestrictedFiles = {"defaults/config.bin",
    "fonts/droid_sans_fb_11x13.bin", "targets/bureau-oled/config.bin", "www/favicon-32x32.png"};

TEST_CASE("Test _write_file Function", "[tools]") {
    init_spiffs();
    const uint8_t testData[] = {0x01, 0x02, 0x03, 0x04};
    size_t testDataSize = sizeof(testData);
    const char* testFileName = "/spiffs/testfile.bin";

    // Test writing valid data
    TEST_ASSERT_TRUE(write_file(testData, testDataSize, testFileName));

    // Verify file size
    struct stat fileInfo;
    TEST_ASSERT_TRUE(get_file_info(&fileInfo, testFileName));
    TEST_ASSERT_EQUAL_UINT32(testDataSize, fileInfo.st_size);

    // Verify file content
    size_t loadedSize;
    uint8_t* loadedData = (uint8_t*)load_file_psram(&loadedSize, testFileName);
    TEST_ASSERT_NOT_NULL(loadedData);
    TEST_ASSERT_EQUAL_UINT32(testDataSize, loadedSize);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(testData, loadedData, testDataSize);
    free(loadedData);
    ESP_LOGI(TAG, "Erasing test file");
    TEST_ASSERT_TRUE(erase_path(testFileName, false));

    // Test writing with null data
    ESP_LOGI(TAG, "Test Invalid write_file with NULL pointer");
    TEST_ASSERT_FALSE(write_file(NULL, testDataSize, testFileName));

    // Test writing with zero size
    ESP_LOGI(TAG, "Test Invalid write_file with data length 0");
    TEST_ASSERT_FALSE(write_file(testData, 0, testFileName));
}

TEST_CASE("Test _open_file Function", "[tools]") {
    init_spiffs();
    const char* testFileName = "/spiffs/test_open_file.bin";
    const char* testFileContent = "Hello, world!";
    size_t contentLength = strlen(testFileContent);

    // Test opening a file for writing
    FILE* writeFile = fopen(testFileName, "w");
    TEST_ASSERT_NOT_NULL(writeFile);
    fwrite(testFileContent, 1, contentLength, writeFile);
    fclose(writeFile);

    // Test opening the same file for reading
    FILE* readFile = fopen(testFileName, "r");
    TEST_ASSERT_NOT_NULL(readFile);
    char buffer[100];
    size_t bytesRead = fread(buffer, 1, contentLength, readFile);
    TEST_ASSERT_EQUAL_UINT32(contentLength, bytesRead);
    buffer[bytesRead] = '\0'; // Null-terminate the string
    TEST_ASSERT_EQUAL_STRING(testFileContent, buffer);
    fclose(readFile);

    // Test opening a file with an invalid mode
    FILE* invalidFile = fopen(testFileName, "invalid_mode");
    TEST_ASSERT_NULL(invalidFile);

    TEST_ASSERT_TRUE(erase_path(testFileName, false));

    // Test opening a non-existent file for reading
    FILE* nonExistentFile = fopen("/spiffs/non_existent_file.bin", "r");
    TEST_ASSERT_NULL(nonExistentFile);
}

TEST_CASE("Test _load_file and _get_file_info Functions", "[tools]") {
    init_spiffs();
    const char* testFileName = "/spiffs/test_load_file.bin";
    const char* testFileContent = "Hello, world!";
    size_t contentLength = strlen(testFileContent);

    // Write test data to a file
    FILE* writeFile = fopen(testFileName, "w");
    TEST_ASSERT_NOT_NULL(writeFile);
    fwrite(testFileContent, 1, contentLength, writeFile);
    fclose(writeFile);

    // Test loading file content
    size_t loadedSize;
    uint8_t* loadedData = (uint8_t*)load_file_psram(&loadedSize, testFileName);
    TEST_ASSERT_NOT_NULL(loadedData);
    TEST_ASSERT_EQUAL_UINT32(contentLength, loadedSize);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(testFileContent, loadedData, contentLength);
    free(loadedData);

    // Test getting file information
    struct stat fileInfo;
    TEST_ASSERT_TRUE(get_file_info(&fileInfo, testFileName));
    TEST_ASSERT_EQUAL_UINT32(contentLength, fileInfo.st_size);
    TEST_ASSERT_TRUE(erase_path(testFileName, false));

    // Test loading a non-existent file
    uint8_t* nonExistentData =
        (uint8_t*)load_file_psram(&loadedSize, "/spiffs/non_existent_file.bin");
    TEST_ASSERT_NULL(nonExistentData);

    // Test getting information for a non-existent file
    TEST_ASSERT_FALSE(get_file_info(&fileInfo, "/spiffs/non_existent_file.bin"));
}

TEST_CASE("Test trim Function", "[tools]") {
    // Test trimming a string with leading and trailing spaces
    std::string str1 = "  Hello World  ";
    TEST_ASSERT_EQUAL_STRING("Hello World", trim(str1).c_str());

    // Test trimming a string with no leading or trailing spaces
    std::string str2 = "Hello World";
    TEST_ASSERT_EQUAL_STRING("Hello World", trim(str2).c_str());

    // Test trimming an empty string
    std::string str3 = "";
    TEST_ASSERT_EQUAL_STRING("", trim(str3).c_str());

    // Test trimming a string with only spaces
    std::string str4 = "     ";
    TEST_ASSERT_EQUAL_STRING("", trim(str4).c_str());
}

TEST_CASE("Test toLowerStr Function", "[tools]") {
    // Test converting a mixed case string to lowercase
    std::string str1 = "Hello World";
    toLowerStr(str1);
    TEST_ASSERT_EQUAL_STRING("hello world", str1.c_str());

    // Test converting an already lowercase string
    std::string str2 = "hello world";
    toLowerStr(str2);
    TEST_ASSERT_EQUAL_STRING("hello world", str2.c_str());

    // Test converting an uppercase string
    std::string str3 = "HELLO WORLD";
    toLowerStr(str3);
    TEST_ASSERT_EQUAL_STRING("hello world", str3.c_str());

    // Test converting an empty string
    std::string str4 = "";
    toLowerStr(str4);
    TEST_ASSERT_EQUAL_STRING("", str4.c_str());

    // Test converting a string with special characters
    std::string str5 = "Hello-World_123";
    toLowerStr(str5);
    TEST_ASSERT_EQUAL_STRING("hello-world_123", str5.c_str());
}
static const std::vector<std::string> testRestrictedPaths = {
    "/spiffs/defaults/file.txt",    // A restricted path
    "/spiffs/fonts/otherfile.txt",  // A restricted path
    "/spiffs/targets/somefile.txt", // A restricted path
    "/spiffs/targets/*.txt",        // A restricted path
    "/spiffs/www/index.html"        // A restricted path
};
TEST_CASE("Test is_restricted_path with restricted paths", "[tools]") {
    for (const std::string& path : testRestrictedPaths) {
        bool result = is_restricted_path(path.c_str());
        TEST_ASSERT_TRUE(result);
    }
}

// Negative Testing
TEST_CASE("Test is_restricted_path with non-restricted paths", "[tools]") {
    const std::vector<std::string> nonRestrictedPaths = {
        "/spiffs/allowed/file.txt",     // Not a restricted path
        "/spiffs/allowed/otherfile.txt" // Not a restricted path
    };

    for (const std::string& path : nonRestrictedPaths) {
        bool result = is_restricted_path(path.c_str());
        TEST_ASSERT_FALSE(result);
    }
}
static const std::vector<std::string> testWildcardPaths = {
    "/spiffs/defaults/file.txt",    // A restricted path
    "/spiffs/fonts/otherfile.txt",  // A restricted path
    "/spiffs/targets/somefile.txt", // A restricted path
    "/spiffs/www/index.html",       // A restricted path
    "/spiffs/defaults/*",           // A path with wildcard
    "/spiffs/fonts/*"               // A path with wildcard
};
TEST_CASE("Test is_restricted_path with wildcards (positive)", "[tools]") {
    for (const std::string& path : testWildcardPaths) {
        bool result = is_restricted_path(path.c_str());
        if (!result) {
            ESP_LOGE(TAG, "Unexpected result. File should be restricted: %s", path.c_str());
        }
        TEST_ASSERT_TRUE(result);
    }
}

TEST_CASE("Test Erase Restricted Path", "[erase_path]") {

    for (const std::string& path : testRestrictedFiles) {
        std::ostringstream fullfilename;
        fullfilename << "/spiffs/" << path;

        // Check if the file exists before erasing
        struct stat fileInfoBefore;
        TEST_ASSERT_TRUE(get_file_info(&fileInfoBefore, fullfilename.str().c_str()));

        // Attempt to erase the restricted path
        TEST_ASSERT_FALSE(erase_path(fullfilename.str().c_str(), true));

        // Check if the file still exists after the erase attempt
        struct stat fileInfoAfter;
        TEST_ASSERT_TRUE(get_file_info(&fileInfoAfter, fullfilename.str().c_str()));
        TEST_ASSERT_EQUAL(fileInfoBefore.st_ino, fileInfoAfter.st_ino);
    }
}

// Test case to attempt erasing restricted files with wildcards
TEST_CASE("Test Erase Restricted Files with Wildcards", "[erase_path]") {
    // Get a list of restricted files based on restrictedPaths
    std::list<tools_file_entry_t> restrictedFiles = get_files_list(std::string("/"));

    for (const tools_file_entry_t& restrictedFile : restrictedFiles) {
        std::string fullfilename = "/" + restrictedFile.name;

        // Check if the file exists before erasing
        struct stat fileInfoBefore;
        TEST_ASSERT_TRUE(get_file_info(&fileInfoBefore, fullfilename.c_str()));

        // Attempt to erase the file with wildcard pattern
        TEST_ASSERT_FALSE(erase_path(fullfilename.c_str(), true));

        // Check if the file still exists after the erase attempt
        struct stat fileInfoAfter;
        TEST_ASSERT_TRUE(get_file_info(&fileInfoAfter, fullfilename.c_str()));
        TEST_ASSERT_EQUAL(fileInfoBefore.st_ino, fileInfoAfter.st_ino);
    }
}
// Test case to create a file and delete it bypassing restrictions
TEST_CASE("Test Create and Delete File Bypassing Restrictions", "[erase_path]") {
    const uint8_t testData[] = {0x01, 0x02, 0x03, 0x04};
    size_t testDataSize = sizeof(testData);
    const char* testFileName = "/spiffs/defaults/test_file.bin";

    // Test writing valid data to create the file
    TEST_ASSERT_TRUE(write_file(testData, testDataSize, testFileName));

    // Verify file size
    struct stat fileInfo;
    TEST_ASSERT_TRUE(get_file_info(&fileInfo, testFileName));
    TEST_ASSERT_EQUAL_UINT32(testDataSize, fileInfo.st_size);

    // Attempt to erase the file with bypassing restrictions (false)
    TEST_ASSERT_TRUE(erase_path(testFileName, false));

    // Verify that the file no longer exists
    TEST_ASSERT_FALSE(get_file_info(&fileInfo, testFileName));
}

// Test function to create a file, check its presence and unrestricted status, and delete it
TEST_CASE("Create, Check, and Delete File in SPIFFS", "[spiffs]") {
    const char* testFileName = "/spiffs/somerandomfile.bin";
    const uint8_t testData[] = {0x01, 0x02, 0x03, 0x04};
    size_t testDataSize = sizeof(testData);

    // Create a file
    TEST_ASSERT_TRUE(write_file(testData, testDataSize, testFileName));

    // Get the list of files
    std::list<tools_file_entry_t> fileList = get_files_list("/spiffs");

    // Check if the new file is in the list and is unrestricted
    bool fileFound = false;
    bool fileUnrestricted = false;
    for (const auto& fileEntry : fileList) {
        if (fileEntry.name == testFileName) {
            fileFound = true;
            fileUnrestricted = !fileEntry.restricted;
            break;
        }
    }

    TEST_ASSERT_TRUE(fileFound);
    TEST_ASSERT_TRUE(fileUnrestricted);

    // Delete the file
    TEST_ASSERT_TRUE(erase_path(testFileName, false)); // Assuming false bypasses restrictions
    fileFound = false;
    // Verify that the file no longer exists
    fileList = get_files_list("/spiffs");
    for (const auto& fileEntry : fileList) {
        if (std::string(testFileName) == fileEntry.name) {
            fileFound = true;
            break;
        }
    }

    TEST_ASSERT_FALSE(fileFound);
}