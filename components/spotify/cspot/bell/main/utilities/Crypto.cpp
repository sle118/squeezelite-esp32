#include "Crypto.h"

#include <mbedtls/base64.h>    // for mbedtls_base64_encode, mbedtls_base64_...
#include <mbedtls/bignum.h>    // for mbedtls_mpi_free, mbedtls_mpi_init
#include <mbedtls/ctr_drbg.h>  // for mbedtls_ctr_drbg_free, mbedtls_ctr_drb...
#include <mbedtls/entropy.h>   // for mbedtls_entropy_free, mbedtls_entropy_...
#include <mbedtls/pkcs5.h>     // for mbedtls_pkcs5_pbkdf2_hmac
#include <cstdint>             // for uint8_t
#include <stdexcept>           // for runtime_error

extern "C" {
#include "aes.h"  // for AES_ECB_decrypt, AES_init_ctx, AES_ctx
}

static unsigned char DHGenerator[1] = {2};

CryptoMbedTLS::CryptoMbedTLS() {}

CryptoMbedTLS::~CryptoMbedTLS() {
  if (aesCtxInitialized) {
    mbedtls_aes_free(&aesCtx);
  }
}

std::vector<uint8_t> CryptoMbedTLS::base64Decode(const std::string& data) {
  // Calculate max decode length
  size_t requiredSize;

  mbedtls_base64_encode(nullptr, 0, &requiredSize, (unsigned char*)data.c_str(),
                        data.size());

  std::vector<uint8_t> output(requiredSize);
  size_t outputLen = 0;
  mbedtls_base64_decode(output.data(), requiredSize, &outputLen,
                        (unsigned char*)data.c_str(), data.size());

  return std::vector<uint8_t>(output.begin(), output.begin() + outputLen);
}

std::string CryptoMbedTLS::base64Encode(const std::vector<uint8_t>& data) {
  // Calculate max output length
  size_t requiredSize;
  mbedtls_base64_encode(nullptr, 0, &requiredSize, data.data(), data.size());

  std::vector<uint8_t> output(requiredSize);
  size_t outputLen = 0;

  mbedtls_base64_encode(output.data(), requiredSize, &outputLen, data.data(),
                        data.size());

  return std::string(output.begin(), output.begin() + outputLen);
}

// Sha1
void CryptoMbedTLS::sha1Init() {
  // Init mbedtls md context, pick sha1
  mbedtls_md_init(&sha1Context);
  mbedtls_md_setup(&sha1Context, mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), 1);
  mbedtls_md_starts(&sha1Context);
}

void CryptoMbedTLS::sha1Update(const std::string& s) {
  sha1Update(std::vector<uint8_t>(s.begin(), s.end()));
}
void CryptoMbedTLS::sha1Update(const std::vector<uint8_t>& vec) {
  mbedtls_md_update(&sha1Context, vec.data(), vec.size());
}

std::vector<uint8_t> CryptoMbedTLS::sha1FinalBytes() {
  std::vector<uint8_t> digest(20);  // SHA1 digest size

  mbedtls_md_finish(&sha1Context, digest.data());
  mbedtls_md_free(&sha1Context);

  return digest;
}

std::string CryptoMbedTLS::sha1Final() {
  auto digest = sha1FinalBytes();
  return std::string(digest.begin(), digest.end());
}

// HMAC SHA1
std::vector<uint8_t> CryptoMbedTLS::sha1HMAC(
    const std::vector<uint8_t>& inputKey, const std::vector<uint8_t>& message) {
  std::vector<uint8_t> digest(20);  // SHA1 digest size

  sha1Init();
  mbedtls_md_hmac_starts(&sha1Context, inputKey.data(), inputKey.size());
  mbedtls_md_hmac_update(&sha1Context, message.data(), message.size());
  mbedtls_md_hmac_finish(&sha1Context, digest.data());
  mbedtls_md_free(&sha1Context);

  return digest;
}

// AES CTR
void CryptoMbedTLS::aesCTRXcrypt(const std::vector<uint8_t>& key,
                                 std::vector<uint8_t>& iv, uint8_t* buffer,
                                 size_t nbytes) {
  if (!aesCtxInitialized) {
    mbedtls_aes_init(&aesCtx);
    aesCtxInitialized = true;
  }
  // needed for internal cache
  size_t off = 0;
  unsigned char streamBlock[16] = {0};

  // set IV
  if (mbedtls_aes_setkey_enc(&aesCtx, key.data(), key.size() * 8) != 0) {
    throw std::runtime_error("Failed to set AES key");
  }
  // Perform decrypt
  if (mbedtls_aes_crypt_ctr(&aesCtx, nbytes, &off, iv.data(), streamBlock,
                            buffer, buffer) != 0) {
    throw std::runtime_error("Failed to decrypt");
  }
}

void CryptoMbedTLS::aesECBdecrypt(const std::vector<uint8_t>& key,
                                  std::vector<uint8_t>& data) {

  struct AES_ctx aesCtr;
  AES_init_ctx(&aesCtr, key.data());

  for (unsigned int x = 0; x < data.size() / 16; x++) {

    AES_ECB_decrypt(&aesCtr, data.data() + (x * 16));
  }
}

// PBKDF2
std::vector<uint8_t> CryptoMbedTLS::pbkdf2HmacSha1(
    const std::vector<uint8_t>& password, const std::vector<uint8_t>& salt,
    int iterations, int digestSize) {
  auto digest = std::vector<uint8_t>(digestSize);

#if MBEDTLS_VERSION_NUMBER < 0x03030000
  // Init sha context
  sha1Init();
  mbedtls_pkcs5_pbkdf2_hmac(&sha1Context, password.data(), password.size(),
                            salt.data(), salt.size(), iterations, digestSize,
                            digest.data());

  // Free sha context
  mbedtls_md_free(&sha1Context);
#else
  mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA1, password.data(),
                                password.size(), salt.data(), salt.size(),
                                iterations, digestSize, digest.data());
#endif

  return digest;
}

void CryptoMbedTLS::dhInit() {
  privateKey = generateVectorWithRandomData(DH_KEY_SIZE);

  // initialize big num
  mbedtls_mpi prime, generator, res, privKey;
  mbedtls_mpi_init(&prime);
  mbedtls_mpi_init(&generator);
  mbedtls_mpi_init(&privKey);
  mbedtls_mpi_init(&res);

  // Read bin into big num mpi
  mbedtls_mpi_read_binary(&prime, DHPrime, sizeof(DHPrime));
  mbedtls_mpi_read_binary(&generator, DHGenerator, sizeof(DHGenerator));
  mbedtls_mpi_read_binary(&privKey, privateKey.data(), DH_KEY_SIZE);

  // perform diffie hellman G^X mod P
  mbedtls_mpi_exp_mod(&res, &generator, &privKey, &prime, NULL);

  // Write generated public key to vector
  this->publicKey = std::vector<uint8_t>(DH_KEY_SIZE);
  mbedtls_mpi_write_binary(&res, publicKey.data(), DH_KEY_SIZE);

  // Release memory
  mbedtls_mpi_free(&prime);
  mbedtls_mpi_free(&generator);
  mbedtls_mpi_free(&privKey);
  mbedtls_mpi_free(&res);
}

std::vector<uint8_t> CryptoMbedTLS::dhCalculateShared(
    const std::vector<uint8_t>& remoteKey) {
  // initialize big num
  mbedtls_mpi prime, remKey, res, privKey;
  mbedtls_mpi_init(&prime);
  mbedtls_mpi_init(&remKey);
  mbedtls_mpi_init(&privKey);
  mbedtls_mpi_init(&res);

  // Read bin into big num mpi
  mbedtls_mpi_read_binary(&prime, DHPrime, sizeof(DHPrime));
  mbedtls_mpi_read_binary(&remKey, remoteKey.data(), remoteKey.size());
  mbedtls_mpi_read_binary(&privKey, privateKey.data(), DH_KEY_SIZE);

  // perform diffie hellman (G^Y)^X mod P (for shared secret)
  mbedtls_mpi_exp_mod(&res, &remKey, &privKey, &prime, NULL);

  auto sharedKey = std::vector<uint8_t>(DH_KEY_SIZE);
  mbedtls_mpi_write_binary(&res, sharedKey.data(), DH_KEY_SIZE);

  // Release memory
  mbedtls_mpi_free(&prime);
  mbedtls_mpi_free(&remKey);
  mbedtls_mpi_free(&privKey);
  mbedtls_mpi_free(&res);

  return sharedKey;
}

// Random stuff
std::vector<uint8_t> CryptoMbedTLS::generateVectorWithRandomData(
    size_t length) {
  std::vector<uint8_t> randomVector(length);
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctrDrbg;
  // Personification string
  const char* pers = "cspotGen";

  // init entropy and random num generator
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctrDrbg);

  // Seed the generator
  mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy,
                        (const unsigned char*)pers, 7);

  // Generate random bytes
  mbedtls_ctr_drbg_random(&ctrDrbg, randomVector.data(), length);

  // Release memory
  mbedtls_entropy_free(&entropy);
  mbedtls_ctr_drbg_free(&ctrDrbg);

  return randomVector;
}
