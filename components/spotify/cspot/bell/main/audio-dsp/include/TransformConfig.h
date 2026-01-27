#pragma once

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace bell {
class TransformConfig {
 protected:
  int invalidInt = -0x7C;
  std::string invalidString = "_invalid";

 public:
  TransformConfig() = default;
  virtual ~TransformConfig() = default;

  int currentVolume = 60;

  virtual std::string rawGetString(const std::string& field) = 0;

  virtual int rawGetInt(const std::string& field) = 0;
  virtual bool isArray(const std::string& field) = 0;

  virtual float rawGetFloat(const std::string& field) = 0;
  virtual std::vector<float> rawGetFloatArray(const std::string& field) = 0;
  virtual std::vector<int> rawGetIntArray(const std::string& field) = 0;

  typedef std::variant<int, float, std::string> Value;
  std::map<std::string, std::vector<Value>> rawValues;

  Value getRawValue(const std::string& field) {
    int index = this->currentVolume * (rawValues[field].size()) / 100;
    if (index >= rawValues[field].size())
      index = rawValues[field].size() - 1;
    return rawValues[field][index];
  }

  std::string getString(const std::string& field, bool isRequired = false,
                        std::string defaultValue = "") {
    if (rawValues.count(field) == 0) {
      rawValues[field] = std::vector<Value>({Value(rawGetString(field))});
    }
    auto val = std::get<std::string>(getRawValue(field));
    if (val == invalidString) {
      if (isRequired)
        throw std::invalid_argument("Field " + field + " is required");
      else
        return defaultValue;
    } else
      return val;
  }

  int getInt(const std::string& field, bool isRequired = false,
             int defaultValue = 0) {
    if (rawValues.count(field) == 0) {
      if (isArray(field)) {
        rawValues[field] = std::vector<Value>();
        for (auto f : rawGetIntArray(field)) {
          rawValues[field].push_back(f);
        }
      } else {
        rawValues[field] = std::vector<Value>({Value(rawGetInt(field))});
      }
    }

    auto val = std::get<int>(getRawValue(field));
    if (val == invalidInt) {
      if (isRequired)
        throw std::invalid_argument("Field " + field + " is required");
      else
        return defaultValue;
    } else
      return val;
  }

  float getFloat(const std::string& field, bool isRequired = false,
                 float defaultValue = 0) {
    if (rawValues.count(field) == 0) {
      if (isArray(field)) {

        rawValues[field] = std::vector<Value>();
        for (auto f : rawGetFloatArray(field)) {
          rawValues[field].push_back(f);
        }
      } else {
        rawValues[field] = std::vector<Value>({Value(rawGetFloat(field))});
      }
    }
    auto val = std::get<float>(getRawValue(field));
    if (val == invalidInt) {
      if (isRequired)
        throw std::invalid_argument("Field " + field + " is required");
      else
        return defaultValue;
    } else
      return val;
  }

  std::vector<int> getChannels() {
    auto channel = getInt("channel", false, invalidInt);

    if (channel != invalidInt) {
      return std::vector<int>({channel});
    }

    return rawGetIntArray("channels");
  }
};
}  // namespace bell