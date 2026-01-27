#pragma once

#include <atomic>      // or std::atomic
#include <functional>  // for function
#include <memory>      // for shared_ptr
#include <string>      // for string

namespace cspot {
struct Context;

class AccessKeyFetcher {
 public:
  AccessKeyFetcher(std::shared_ptr<cspot::Context> ctx);

  /**
  * @brief Checks if key is expired
  * @returns true when currently held access key is not valid
  */
  bool isExpired();

  /**
  * @brief Fetches a new access key
  * @remark In case the key is expired, this function blocks until a refresh is done.
  * @returns access key
  */
  std::string getAccessKey();

  /**
  * @brief Forces a refresh of the access key
  */
  void updateAccessKey();

 private:
  std::shared_ptr<cspot::Context> ctx;

  std::atomic<bool> keyPending = false;
  std::string accessKey;
  long long int expiresAt;
};
}  // namespace cspot
