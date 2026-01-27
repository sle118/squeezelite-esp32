
#pragma once

#include <stddef.h>     // for size_t
#include <cstdint>      // for uint8_t, int32_t
#include <memory>       // for make_unique, unique_ptr
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for pair
#include <vector>       // for vector

#include "SocketStream.h"  // for SocketStream
#include "URLParser.h"     // for URLParser
#ifndef BELL_DISABLE_FMT
#include "fmt/core.h"  // for format
#endif
#include "picohttpparser.h"  // for phr_header

namespace bell {
class HTTPClient {
 public:
  // most basic header type, represents by a key-val
  typedef std::pair<std::string, std::string> ValueHeader;

  typedef std::vector<ValueHeader> Headers;

  // Helper over ValueHeader, formatting a HTTP bytes range
  struct RangeHeader {
    static ValueHeader range(int32_t from, int32_t to) {
#ifndef BELL_DISABLE_FMT
      return ValueHeader{"Range", fmt::format("bytes={}-{}", from, to)};
#else
      return ValueHeader{
          "Range", "bytes=" + std::to_string(from) + "-" + std::to_string(to)};
#endif
    }

    static ValueHeader last(int32_t nbytes) {
#ifndef BELL_DISABLE_FMT
      return ValueHeader{"Range", fmt::format("bytes=-{}", nbytes)};
#else
      return ValueHeader{"Range", "bytes=-" + std::to_string(nbytes)};
#endif
    }
  };

  class Response {
   public:
    Response(){};
    ~Response();

    /**
    * Initializes a connection with a given url.
    */
    void connect(const std::string& url);

    void rawRequest(const std::string& method, const std::string& url,
                    const std::vector<uint8_t>& content, Headers& headers);
    void get(const std::string& url, Headers headers = {});
    void post(const std::string& url, Headers headers = {},
              const std::vector<uint8_t>& body = {});

    std::string_view body();
    std::vector<uint8_t> bytes();

    std::string_view header(const std::string& headerName);
    bell::SocketStream& stream() { return this->socketStream; }

    size_t contentLength();
    size_t totalLength();

   private:
    bell::URLParser urlParser;
    bell::SocketStream socketStream;

    struct phr_header phResponseHeaders[32];
    const size_t HTTP_BUF_SIZE = 1024;

    std::vector<uint8_t> httpBuffer = std::vector<uint8_t>(HTTP_BUF_SIZE);
    std::vector<uint8_t> rawBody = std::vector<uint8_t>();
    size_t httpBufferAvailable;

    size_t contentSize = 0;
    bool hasContentSize = false;

    Headers responseHeaders;

    void readResponseHeaders();
    void readRawBody();
  };

  enum class Method : uint8_t { GET = 0, POST = 1 };

  struct Request {
    std::string url;
    Method method;
    Headers headers;
  };

  static std::unique_ptr<Response> get(const std::string& url,
                                       Headers headers = {}) {
    auto response = std::make_unique<Response>();
    response->connect(url);
    response->get(url, headers);
    return response;
  }

  static std::unique_ptr<Response> post(const std::string& url,
                                        Headers headers = {},
                                        const std::vector<uint8_t>& body = {}) {
    auto response = std::make_unique<Response>();
    response->connect(url);
    response->post(url, headers, body);
    return response;
  }
};
}  // namespace bell
