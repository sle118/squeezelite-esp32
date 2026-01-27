#pragma once

#include "TransformConfig.h"
#include "cJSON.h"

namespace bell {
class JSONTransformConfig : public bell::TransformConfig {
 private:
  cJSON* json;

 public:
  JSONTransformConfig(cJSON* body) { this->json = body; };
  ~JSONTransformConfig(){};

  std::string rawGetString(const std::string& field) override {
    cJSON* value = cJSON_GetObjectItem(json, field.c_str());

    if (value != NULL && cJSON_IsString(value)) {
      return std::string(value->valuestring);
    }

    return "invalid";
  }

  std::vector<int> rawGetIntArray(const std::string& field) override {
    std::vector<int> result;

    cJSON* value = cJSON_GetObjectItem(json, field.c_str());

    if (value != NULL && cJSON_IsArray(value)) {
      for (int i = 0; i < cJSON_GetArraySize(value); i++) {
        cJSON* item = cJSON_GetArrayItem(value, i);
        if (item != NULL && cJSON_IsNumber(item)) {
          result.push_back(item->valueint);
        }
      }
    }

    return result;
  }

  std::vector<float> rawGetFloatArray(const std::string& field) override {
    std::vector<float> result;

    cJSON* value = cJSON_GetObjectItem(json, field.c_str());

    if (value != NULL && cJSON_IsArray(value)) {
      for (int i = 0; i < cJSON_GetArraySize(value); i++) {
        cJSON* item = cJSON_GetArrayItem(value, i);
        if (item != NULL && cJSON_IsNumber(item)) {
          result.push_back(item->valuedouble);
        }
      }
    }

    return result;
  }

  int rawGetInt(const std::string& field) override {
    cJSON* value = cJSON_GetObjectItem(json, field.c_str());

    if (value != NULL && cJSON_IsNumber(value)) {
      return (int)value->valueint;
    }

    return invalidInt;
  }

  bool isArray(const std::string& field) override {
    cJSON* value = cJSON_GetObjectItem(json, field.c_str());

    if (value != NULL && cJSON_IsArray(value)) {
      return true;
    }

    return false;
  }

  float rawGetFloat(const std::string& field) override {
    cJSON* value = cJSON_GetObjectItem(json, field.c_str());

    if (value != NULL && cJSON_IsNumber(value)) {
      return (float)value->valuedouble;
    }

    return invalidInt;
  }
};
}  // namespace bell