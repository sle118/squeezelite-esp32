#pragma once

#include <cstdint>  // for uint8_t
#include <memory>   // for unique_ptr
#include <string>   // for string
#include <vector>   // for vector

#include "Crypto.h"                      // for Crypto
#include "protobuf/authentication.pb.h"  // for ClientResponseEncrypted
#include "protobuf/keyexchange.pb.h"     // for APResponseMessage, ClientHello

namespace cspot {
class AuthChallenges {
 public:
  AuthChallenges();
  ~AuthChallenges();

  /**
  * @brief Prepares a spotify authentication packet
  * @param authBlob authentication blob bytes
  * @param authType value representing spotify's authentication type
  * @param deviceId device id to use during auth.
  * @param username spotify's username
  *
  * @returns vector containing bytes of the authentication packet
  */
  std::vector<uint8_t> prepareAuthPacket(std::vector<uint8_t>& authBlob,
                                         int authType,
                                         const std::string& deviceId,
                                         const std::string& username);

  /**
  * @brief Solves the ApHello packet, and returns a packet with response
  *
  * @param helloPacket hello packet bytes received from the server
  * @param data authentication data received from the server
  *
  * @returns vector containing response packet
  */
  std::vector<uint8_t> solveApHello(std::vector<uint8_t>& helloPacket,
                                    std::vector<uint8_t>& data);

  /**
  * @brief Prepares an client hello packet, used for initial auth with spotify
  *
  * @returns vector containing the packet's data
  */
  std::vector<uint8_t> prepareClientHello();

  std::vector<uint8_t> shanSendKey = {};
  std::vector<uint8_t> shanRecvKey = {};

 private:
  const long long SPOTIFY_VERSION = 0x10800000000;

  // Protobuf structures
  ClientResponseEncrypted authRequest;
  ClientResponsePlaintext clientResPlaintext;
  ClientHello clientHello;
  APResponseMessage apResponse;

  std::unique_ptr<Crypto> crypto;
};
}  // namespace cspot
