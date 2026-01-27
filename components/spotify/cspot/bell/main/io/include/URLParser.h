#pragma once

#include <cstdlib>    // for strtol, size_t
#include <regex>      // for match_results, match_results<>::value_type, sub...
#include <stdexcept>  // for invalid_argument
#include <string>     // for string, allocator, operator+, char_traits, oper...

namespace bell {
class URLParser {
 public:
  static std::string urlEncode(const std::string& value) {
    std::string new_str = "";
    static auto hex_digt = "0123456789ABCDEF";

    std::string result;
    result.reserve(value.size() << 1);

    for (auto ch : value) {
      if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z') || ch == '-' || ch == '_' || ch == '!' ||
          ch == '\'' || ch == '(' || ch == ')' || ch == '*' || ch == '~' ||
          ch == '.')  // !'()*-._~
      {
        result.push_back(ch);
      } else {
        result += std::string("%") +
                  hex_digt[static_cast<unsigned char>(ch) >> 4] +
                  hex_digt[static_cast<unsigned char>(ch) & 15];
      }
    }

    return result;
  }
  static std::string urlDecode(const std::string& value) {
    std::string result;
    result.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i) {
      auto ch = value[i];

      if (ch == '%' && (i + 2) < value.size()) {
        auto hex = value.substr(i + 1, 2);
        auto dec = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
        result.push_back(dec);
        i += 2;
      } else if (ch == '+') {
        result.push_back(' ');
      } else {
        result.push_back(ch);
      }
    }

    return result;
  }

  std::string host;
  int port = -1;

  std::string schema = "http";
  std::string path;
#ifdef BELL_DISABLE_REGEX
  void parse(const char* url, std::vector<std::string>& match);
#else
  static const std::regex urlParseRegex;
#endif

  static URLParser parse(const std::string& url) {
    URLParser parser;

    // apply parser.urlParseRegex to url
#ifdef BELL_DISABLE_REGEX
    std::vector<std::string> match(6);
    parser.parse(url.c_str(), match);
#else
    std::cmatch match;
    std::regex_match(url.c_str(), match, parser.urlParseRegex);
#endif

    if (match.size() < 3) {
      throw std::invalid_argument("Invalid URL");
    }

    parser.schema = match[1];
    parser.host = match[2];
    parser.path = match[3];

    if (match[4] != "") {
      parser.path += match[4];
    }

    // check if parser.host contains ':'
    if (parser.host.find(':') != std::string::npos) {
      auto port = std::stoi(
          parser.host.substr(parser.host.find(':') + 1, parser.host.size()));
      auto host = parser.host.substr(0, parser.host.find(':'));
      parser.port = port;
      parser.host = host;
    }

    if (parser.port == -1) {
      parser.port = parser.schema == "http" ? 80 : 443;
    }

    return parser;
  }
};
}  // namespace bell