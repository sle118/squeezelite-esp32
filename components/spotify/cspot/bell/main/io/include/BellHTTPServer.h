#pragma once

#include <BellLogger.h>   // for bell
#include <stdint.h>       // for uint8_t
#include <stdlib.h>       // for free, size_t
#include <functional>     // for function
#include <map>            // for map
#include <memory>         // for unique_ptr
#include <mutex>          // for mutex
#include <string>         // for string, hash, operator==, operator<
#include <unordered_map>  // for unordered_map
#include <utility>        // for pair
#include <vector>         // for vector

#include "CivetServer.h"  // for CivetServer, CivetHandler

using namespace bell;
namespace bell {
class BellHTTPServer : public CivetHandler {
 public:
  BellHTTPServer(int serverPort);
  ~BellHTTPServer();

  enum class WSState { CONNECTED, READY, CLOSED };

  struct HTTPResponse {
    uint8_t* body;
    size_t bodySize;
    std::map<std::string, std::string> headers;

    int status;

    HTTPResponse() {
      body = nullptr;
      bodySize = 0;
      status = 200;
    }

    ~HTTPResponse() {
      if (body != nullptr) {
        free(body);
        body = nullptr;
      }
    }
  };
  typedef std::function<std::unique_ptr<HTTPResponse>(
      struct mg_connection* conn)>
      HTTPHandler;
  typedef std::function<void(struct mg_connection* conn, WSState)>
      WSStateHandler;
  typedef std::function<void(struct mg_connection* conn, char*, size_t)>
      WSDataHandler;

  class Router {
   public:
    struct RouterNode {
      std::unordered_map<std::string, std::unique_ptr<RouterNode>> children;
      HTTPHandler value = nullptr;
      std::string paramName = "";

      bool isParam = false;
      bool isCatchAll = false;
    };

    RouterNode root = RouterNode();

    typedef std::unordered_map<std::string, std::string> Params;
    typedef std::pair<HTTPHandler, Params> HandlerAndParams;

    std::vector<std::string> split(const std::string str,
                                   const std::string regex_str);

    void insert(const std::string& route, HTTPHandler& value);

    HandlerAndParams find(const std::string& route);
  };

  std::vector<int> getListeningPorts() { return server->getListeningPorts(); };
  void close() { server->close(); }

  std::unique_ptr<HTTPResponse> makeJsonResponse(const std::string& json,
                                                 int status = 200);
  std::unique_ptr<HTTPResponse> makeEmptyResponse();

  void registerNotFound(HTTPHandler handler);
  void registerGet(const std::string&, HTTPHandler handler);
  void registerPost(const std::string&, HTTPHandler handler);
  void registerWS(const std::string&, WSDataHandler dataHandler,
                  WSStateHandler stateHandler);

  static std::unordered_map<std::string, std::string> extractParams(
      struct mg_connection* conn);

 private:
  std::unique_ptr<CivetServer> server;
  std::vector<std::string> civetWebOptions;
  int serverPort = 8080;

  Router getRequestsRouter;
  Router postRequestsRouter;
  std::mutex responseMutex;
  HTTPHandler notFoundHandler;

  static std::mutex initMutex;

  bool handleGet(CivetServer* server, struct mg_connection* conn);
  bool handlePost(CivetServer* server, struct mg_connection* conn);
};

}  // namespace bell
