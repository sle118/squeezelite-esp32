#include "HTTPClient.h"

#include <string.h>   // for memcpy
#include <algorithm>  // for transform
#include <cassert>    // for assert
#include <cctype>     // for tolower
#include <ostream>    // for operator<<, basic_ostream
#include <stdexcept>  // for runtime_error

#include "BellSocket.h"  // for bell

using namespace bell;

void HTTPClient::Response::connect(const std::string& url) {
  urlParser = bell::URLParser::parse(url);

  // Open socket of type
  this->socketStream.open(urlParser.host, urlParser.port,
                          urlParser.schema == "https");
}

HTTPClient::Response::~Response() {
  if (this->socketStream.isOpen()) {
    this->socketStream.close();
  }
}

void HTTPClient::Response::rawRequest(const std::string& url,
                                      const std::string& method,
                                      const std::vector<uint8_t>& content,
                                      Headers& headers) {
  urlParser = bell::URLParser::parse(url);

  // Prepare a request
  const char* reqEnd = "\r\n";

  for (int attempt = 0;; attempt++) {
    // Reconnect when the socket was never opened, was closed (for example
    // a keep-alive connection dropped by the server), or the stream is in
    // a failed state left over from a previous request
    if (!socketStream.isOpen() || !socketStream.good()) {
      socketStream.close();
      socketStream.clear();
      socketStream.open(urlParser.host, urlParser.port,
                        urlParser.schema == "https");
      if (!socketStream.good()) {
        throw std::runtime_error("Cannot connect to " + urlParser.host);
      }
    }

    socketStream << method << " " << urlParser.path << " HTTP/1.1" << reqEnd;
    socketStream << "Host: " << urlParser.host << ":" << urlParser.port
                 << reqEnd;
    socketStream << "Connection: keep-alive" << reqEnd;
    socketStream << "Accept: */*" << reqEnd;

    // Write content
    if (content.size() > 0) {
      socketStream << "Content-Length: " << content.size() << reqEnd;
    }

    // Write headers
    for (auto& header : headers) {
      socketStream << header.first << ": " << header.second << reqEnd;
    }

    socketStream << reqEnd;

    // Write request body
    if (content.size() > 0) {
      socketStream.write((const char*)content.data(), content.size());
    }

    socketStream.flush();

    // Parse response
    try {
      readResponseHeaders();
      return;
    } catch (const std::runtime_error&) {
      // Only retry when the connection itself died (stale keep-alive
      // socket closed by the server); genuine protocol errors leave the
      // stream in good state and are rethrown immediately
      if (attempt >= 1 || socketStream.good()) {
        throw;
      }
      socketStream.close();
    }
  }
}

void HTTPClient::Response::readResponseHeaders() {
  char *method, *path;
  const char* msgPointer;

  size_t msgLen;
  int pret, minorVersion, status;

  size_t prevbuflen = 0, numHeaders;
  this->httpBufferAvailable = 0;

  while (1) {
    socketStream.getline((char*)httpBuffer.data() + httpBufferAvailable,
                         httpBuffer.size() - httpBufferAvailable);

    // A connection closed by the peer or a socket error leaves the stream
    // in eof/fail state with nothing extracted. Without this check the
    // loop spins forever at 100% CPU, as getline() keeps returning
    // immediately with gcount() == 0 once the stream has failed.
    if (socketStream.eof() || socketStream.bad() ||
        socketStream.gcount() == 0) {
      throw std::runtime_error("Connection closed while reading HTTP headers");
    }

    // getline() hit the buffer limit before finding '\n'; the stream is in
    // fail state and no further progress is possible
    if (socketStream.fail()) {
      throw std::runtime_error("Response too large");
    }

    prevbuflen = httpBufferAvailable;
    httpBufferAvailable += socketStream.gcount();

    // Restore delimiters: getline() consumed the '\n' and wrote a '\0'
    // terminator, put "\r\n" back for the parser. Guarded so a 1-byte
    // first line cannot write before the start of the buffer.
    if (httpBufferAvailable >= 2)
      memcpy(httpBuffer.data() + httpBufferAvailable - 2, "\r\n", 2);

    // Parse the request
    numHeaders = sizeof(phResponseHeaders) / sizeof(phResponseHeaders[0]);

    pret =
        phr_parse_response((const char*)httpBuffer.data(), httpBufferAvailable,
                           &minorVersion, &status, &msgPointer, &msgLen,
                           phResponseHeaders, &numHeaders, prevbuflen);

    if (pret > 0) {
      break; /* successfully parsed the request */
    } else if (pret == -1)
      throw std::runtime_error("Cannot parse http response");

    /* request is incomplete, continue the loop */
    assert(pret == -2);
    if (httpBufferAvailable == httpBuffer.size())
      throw std::runtime_error("Response too large");
  }

  this->responseHeaders = {};

  // Headers have benen read
  for (int headerIndex = 0; headerIndex < numHeaders; headerIndex++) {
    this->responseHeaders.push_back(
        ValueHeader{std::string(phResponseHeaders[headerIndex].name,
                                phResponseHeaders[headerIndex].name_len),
                    std::string(phResponseHeaders[headerIndex].value,
                                phResponseHeaders[headerIndex].value_len)});
  }

  std::string contentLengthValue = std::string(header("content-length"));
  if (contentLengthValue.size() > 0) {
    this->hasContentSize = true;
    this->contentSize = std::stoi(contentLengthValue);
  }
}

void HTTPClient::Response::get(const std::string& url, Headers headers) {
  std::string method = "GET";
  return this->rawRequest(url, method, {}, headers);
}

void HTTPClient::Response::post(const std::string& url, Headers headers,
                                const std::vector<uint8_t>& body) {
  std::string method = "POST";
  return this->rawRequest(url, method, body, headers);
}

size_t HTTPClient::Response::contentLength() {
  return contentSize;
}

std::string_view HTTPClient::Response::header(const std::string& headerName) {
  for (auto& header : this->responseHeaders) {
    std::string headerValue = header.first;
    std::transform(headerValue.begin(), headerValue.end(), headerValue.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (headerName == headerValue) {
      return header.second;
    }
  }

  return "";
}

size_t HTTPClient::Response::totalLength() {
  auto rangeHeader = header("content-range");

  if (rangeHeader.find("/") != std::string::npos) {
    return std::stoi(
        std::string(rangeHeader.substr(rangeHeader.find("/") + 1)));
  }

  return this->contentLength();
}

void HTTPClient::Response::readRawBody() {
  if (contentSize > 0 && rawBody.size() == 0) {
    rawBody = std::vector<uint8_t>(contentSize);
    socketStream.read((char*)rawBody.data(), contentSize);
  }
}

std::string_view HTTPClient::Response::body() {
  readRawBody();
  return std::string_view((char*)rawBody.data(), rawBody.size());
}

std::vector<uint8_t> HTTPClient::Response::bytes() {
  readRawBody();
  return rawBody;
}
