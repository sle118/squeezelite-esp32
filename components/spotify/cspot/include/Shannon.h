#ifndef SHANNON_H
#define SHANNON_H

#include <cstdint>  // for uint32_t, uint8_t
#include <vector>   // for vector

class Shannon {
 public:
  static constexpr unsigned int N = 16;

  void key(const std::vector<uint8_t>& key);     /* set key */
  void nonce(const std::vector<uint8_t>& nonce); /* set Init Vector */
  void stream(std::vector<uint8_t>& buf);        /* stream cipher */
  void maconly(std::vector<uint8_t>& buf);       /* accumulate MAC */
  void encrypt(std::vector<uint8_t>& buf);       /* encrypt + MAC */
  void decrypt(std::vector<uint8_t>& buf);       /* finalize + MAC */
  void finish(std::vector<uint8_t>& buf);        /* finalise MAC */

 private:
  static constexpr unsigned int FOLD = Shannon::N;
  static constexpr unsigned int INITKONST = 0x6996c53a;
  static constexpr unsigned int KEYP = 13;
  uint32_t R[Shannon::N];
  uint32_t CRC[Shannon::N];
  uint32_t initR[Shannon::N];
  uint32_t konst;
  uint32_t sbuf;
  uint32_t mbuf;
  int nbuf;
  static uint32_t sbox1(uint32_t w);
  static uint32_t sbox2(uint32_t w);
  void cycle();
  void crcfunc(uint32_t i);
  void macfunc(uint32_t i);
  void initState();
  void saveState();
  void reloadState();
  void genkonst();
  void diffuse();
  void loadKey(const std::vector<uint8_t>& key);
};

#endif