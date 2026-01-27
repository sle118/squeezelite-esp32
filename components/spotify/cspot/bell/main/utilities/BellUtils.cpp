#include "BellUtils.h"

#include <stdlib.h>  // for free
#include <random>    // for mt19937, uniform_int_distribution, random_device
#ifdef ESP_PLATFORM
#include "esp_system.h"
#if __has_include("esp_mac.h")
#include "esp_mac.h"
#endif
#endif

std::string bell::generateRandomUUID() {
  static std::random_device dev;
  static std::mt19937 rng(dev());

  std::uniform_int_distribution<int> dist(0, 15);

  const char* v = "0123456789abcdef";
  const bool dash[] = {0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0};

  std::string res;
  for (int i = 0; i < 16; i++) {
    if (dash[i])
      res += "-";
    res += v[dist(rng)];
    res += v[dist(rng)];
  }
  return res;
}

std::string bell::getMacAddress() {
#ifdef ESP_PLATFORM

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char macStr[18];
  sprintf(macStr, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
          mac[3], mac[4], mac[5]);
  return std::string(macStr);
#endif
  return "00:00:00:00:00:00";
}

void bell::freeAndNull(void*& ptr) {
  free(ptr);
  ptr = nullptr;
}
