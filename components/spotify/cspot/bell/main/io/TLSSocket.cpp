#include "TLSSocket.h"

#include <mbedtls/ctr_drbg.h>     // for mbedtls_ctr_drbg_free, mbedtls_ctr_...
#include <mbedtls/entropy.h>      // for mbedtls_entropy_free, mbedtls_entro...
#include <mbedtls/net_sockets.h>  // for mbedtls_net_connect, mbedtls_net_free
#include <mbedtls/ssl.h>          // for mbedtls_ssl_conf_authmode, mbedtls_...
#include <cstring>                // for strlen, NULL
#include <stdexcept>              // for runtime_error

#include "BellLogger.h"  // for AbstractLogger, BELL_LOG
#include "X509Bundle.h"  // for shouldVerify, attach

/**
 * Platform TLSSocket implementation for the mbedtls
 */
bell::TLSSocket::TLSSocket() {
  this->isClosed = false;
  mbedtls_net_init(&server_fd);
  mbedtls_ssl_init(&ssl);
  mbedtls_ssl_config_init(&conf);

  if (bell::X509Bundle::shouldVerify()) {
    bell::X509Bundle::attach(&conf);
  }

  mbedtls_ctr_drbg_init(&ctr_drbg);
  mbedtls_entropy_init(&entropy);

  const char* pers = "euphonium";
  int ret;
  if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                   (const unsigned char*)pers, strlen(pers))) !=
      0) {
    BELL_LOG(error, "http_tls",
             "failed\n  ! mbedtls_ctr_drbg_seed returned %d\n", ret);
    throw std::runtime_error("mbedtls_ctr_drbg_seed failed");
  }
}

void bell::TLSSocket::open(const std::string& hostUrl, uint16_t port) {
  int ret;
  if ((ret = mbedtls_net_connect(&server_fd, hostUrl.c_str(),
                                 std::to_string(port).c_str(),
                                 MBEDTLS_NET_PROTO_TCP)) != 0) {
    BELL_LOG(error, "http_tls", "failed! connect returned %d\n", ret);
  }

  if ((ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {

    BELL_LOG(error, "http_tls", "failed! config returned %d\n", ret);
    throw std::runtime_error("mbedtls_ssl_config_defaults failed");
  }

  // Only verify if the X509 bundle is present
  if (bell::X509Bundle::shouldVerify()) {
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  } else {
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
  }

  mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
  mbedtls_ssl_setup(&ssl, &conf);

  if ((ret = mbedtls_ssl_set_hostname(&ssl, hostUrl.c_str())) != 0) {
    throw std::runtime_error("mbedtls_ssl_set_hostname failed");
  }
  mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv,
                      NULL);

  while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      BELL_LOG(error, "http_tls", "failed! config returned %d\n", ret);
      throw std::runtime_error("mbedtls_ssl_handshake error");
    }
  }
}

size_t bell::TLSSocket::read(uint8_t* buf, size_t len) {
  return mbedtls_ssl_read(&ssl, buf, len);
}

size_t bell::TLSSocket::write(uint8_t* buf, size_t len) {
  return mbedtls_ssl_write(&ssl, buf, len);
}

size_t bell::TLSSocket::poll() {
  return mbedtls_ssl_get_bytes_avail(&ssl);
}
bool bell::TLSSocket::isOpen() {
  return !isClosed;
}

void bell::TLSSocket::close() {
  if (!isClosed) {
    mbedtls_net_free(&server_fd);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    this->isClosed = true;
  }
}
