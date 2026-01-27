#include "Utils.h"

#include <stdlib.h>  // for strtol
#include <chrono>
#include <iomanip>      // for operator<<, setfill, setw
#include <iostream>     // for basic_ostream, hex
#include <sstream>      // for stringstream
#include <string>       // for string
#include <type_traits>  // for enable_if<>::type
#ifndef _WIN32
#include <arpa/inet.h>
#endif

unsigned long long getCurrentTimestamp() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

uint64_t hton64(uint64_t value) {
  int num = 42;
  if (*(char*)&num == 42) {
    uint32_t high_part = htonl((uint32_t)(value >> 32));
    uint32_t low_part = htonl((uint32_t)(value & 0xFFFFFFFFLL));
    return (((uint64_t)low_part) << 32) | high_part;
  } else {
    return value;
  }
}

std::vector<uint8_t> stringHexToBytes(const std::string& s) {
  std::vector<uint8_t> v;
  v.reserve(s.length() / 2);

  for (std::string::size_type i = 0; i < s.length(); i += 2) {
    std::string byteString = s.substr(i, 2);
    uint8_t byte = (uint8_t)strtol(byteString.c_str(), NULL, 16);
    v.push_back(byte);
  }

  return v;
}

std::string bytesToHexString(const std::vector<uint8_t>& v) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  std::vector<uint8_t>::const_iterator it;

  for (it = v.begin(); it != v.end(); it++) {
    ss << std::setw(2) << static_cast<unsigned>(*it);
  }

  return ss.str();
}

std::vector<uint8_t> bigNumAdd(std::vector<uint8_t> num, int n) {
  auto carry = n;
  for (int x = num.size() - 1; x >= 0; x--) {
    int res = num[x] + carry;
    if (res < 256) {
      carry = 0;
      num[x] = res;
    } else {
      // Carry the rest of the division
      carry = res / 256;
      num[x] = res % 256;

      // extend the vector at the last index
      if (x == 0) {
        num.insert(num.begin(), carry);
        return num;
      }
    }
  }

  return num;
}

std::vector<uint8_t> bigNumDivide(std::vector<uint8_t> num, int n) {
  auto carry = 0;
  for (int x = 0; x < num.size(); x++) {
    int res = num[x] + carry * 256;
    if (res < n) {
      carry = res;
      num[x] = 0;
    } else {
      // Carry the rest of the division
      carry = res % n;
      num[x] = res / n;
    }
  }

  return num;
}

std::vector<uint8_t> bigNumMultiply(std::vector<uint8_t> num, int n) {
  auto carry = 0;
  for (int x = num.size() - 1; x >= 0; x--) {
    int res = num[x] * n + carry;
    if (res < 256) {
      carry = 0;
      num[x] = res;
    } else {
      // Carry the rest of the division
      carry = res / 256;
      num[x] = res % 256;

      // extend the vector at the last index
      if (x == 0) {
        num.insert(num.begin(), carry);
        return num;
      }
    }
  }

  return num;
}
unsigned char h2int(char c) {
  if (c >= '0' && c <= '9') {
    return ((unsigned char)c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return ((unsigned char)c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F') {
    return ((unsigned char)c - 'A' + 10);
  }
  return (0);
}

std::string urlDecode(std::string str) {
  std::string encodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++) {
    c = str[i];
    if (c == '+') {
      encodedString += ' ';
    } else if (c == '%') {
      i++;
      code0 = str[i];
      i++;
      code1 = str[i];
      c = (h2int(code0) << 4) | h2int(code1);
      encodedString += c;
    } else {

      encodedString += c;
    }
  }

  return encodedString;
}