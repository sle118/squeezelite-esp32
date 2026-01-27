#include "ShannonConnection.h"

#include <type_traits>  // for remove_extent_t

#include "BellLogger.h"       // for AbstractLogger
#include "Logger.h"           // for CSPOT_LOG
#include "Packet.h"           // for Packet, cspot
#include "PlainConnection.h"  // for PlainConnection
#include "Shannon.h"          // for Shannon
#include "Utils.h"            // for pack, extract
#ifndef _WIN32
#include <arpa/inet.h>
#endif

using namespace cspot;

ShannonConnection::ShannonConnection() {}

ShannonConnection::~ShannonConnection() {}

void ShannonConnection::wrapConnection(
    std::shared_ptr<cspot::PlainConnection> conn, std::vector<uint8_t>& sendKey,
    std::vector<uint8_t>& recvKey) {
  this->conn = conn;

  this->sendCipher = std::make_unique<Shannon>();
  this->recvCipher = std::make_unique<Shannon>();

  // Set keys
  this->sendCipher->key(sendKey);
  this->recvCipher->key(recvKey);

  // Set initial nonce
  this->sendCipher->nonce(pack<uint32_t>(htonl(0)));
  this->recvCipher->nonce(pack<uint32_t>(htonl(0)));
}

void ShannonConnection::sendPacket(uint8_t cmd, std::vector<uint8_t>& data) {
  std::scoped_lock lock(this->writeMutex);
  auto rawPacket = this->cipherPacket(cmd, data);

  // Shannon encrypt the packet and write it to sock
  this->sendCipher->encrypt(rawPacket);
  this->conn->writeBlock(rawPacket);

  // Generate mac
  std::vector<uint8_t> mac(MAC_SIZE);
  this->sendCipher->finish(mac);

  // Update the nonce
  this->sendNonce += 1;
  this->sendCipher->nonce(pack<uint32_t>(htonl(this->sendNonce)));

  // Write the mac to sock
  this->conn->writeBlock(mac);
}

cspot::Packet ShannonConnection::recvPacket() {
  std::scoped_lock lock(this->readMutex);

  std::vector<uint8_t> data(3);
  // Receive 3 bytes, cmd + int16 size
  this->conn->readBlock(data.data(), 3);
  this->recvCipher->decrypt(data);

  auto readSize = ntohs(extract<uint16_t>(data, 1));
  auto packetData = std::vector<uint8_t>(readSize);

  // Read and decode if the packet has an actual body
  if (readSize > 0) {
    this->conn->readBlock(packetData.data(), readSize);
    this->recvCipher->decrypt(packetData);
  }

  // Read mac
  std::vector<uint8_t> mac(MAC_SIZE);
  this->conn->readBlock(mac.data(), MAC_SIZE);

  // Generate mac
  std::vector<uint8_t> mac2(MAC_SIZE);
  this->recvCipher->finish(mac2);

  if (mac != mac2) {
    CSPOT_LOG(error, "Shannon read: Mac doesn't match");
  }

  // Update the nonce
  this->recvNonce += 1;
  this->recvCipher->nonce(pack<uint32_t>(htonl(this->recvNonce)));
  uint8_t cmd = 0;
  if (data.size() > 0) {
    cmd = data[0];
  }
  // data[0] == cmd
  return Packet{cmd, packetData};
}

std::vector<uint8_t> ShannonConnection::cipherPacket(
    uint8_t cmd, std::vector<uint8_t>& data) {
  // Generate packet structure, [Command] [Size] [Raw data]
  auto sizeRaw = pack<uint16_t>(htons(uint16_t(data.size())));

  sizeRaw.insert(sizeRaw.begin(), cmd);
  sizeRaw.insert(sizeRaw.end(), data.begin(), data.end());

  return sizeRaw;
}
