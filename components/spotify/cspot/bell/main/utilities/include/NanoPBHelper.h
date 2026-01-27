#pragma once

#include <stdint.h>  // for uint8_t
#include <stdio.h>   // for printf
#include <string>    // for string
#include <vector>    // for vector

#include "pb.h"         // for pb_msgdesc_t, pb_bytes_array_t, PB_GET_ERROR
#include "pb_decode.h"  // for pb_istream_from_buffer, pb_decode, pb_istream_s

std::vector<uint8_t> pbEncode(const pb_msgdesc_t* fields,
                              const void* src_struct);

pb_bytes_array_t* vectorToPbArray(const std::vector<uint8_t>& vectorToPack);

void packString(char*& dst, std::string stringToPack);

std::vector<uint8_t> pbArrayToVector(pb_bytes_array_t* pbArray);

template <typename T>
T pbDecode(const pb_msgdesc_t* fields, std::vector<uint8_t>& data) {

  T result = {};
  // Create stream
  pb_istream_t stream = pb_istream_from_buffer(&data[0], data.size());

  // Decode the message
  if (pb_decode(&stream, fields, &result) == false) {
    printf("Decode failed: %s\n", PB_GET_ERROR(&stream));
  }

  return result;
}

template <typename T>
void pbDecode(T& result, const pb_msgdesc_t* fields,
              std::vector<uint8_t>& data) {
  // Create stream
  pb_istream_t stream = pb_istream_from_buffer(&data[0], data.size());

  // Decode the message
  if (pb_decode(&stream, fields, &result) == false) {
    printf("Decode failed: %s\n", PB_GET_ERROR(&stream));
  }
}

void pbPutString(const std::string& stringToPack, char* dst);
void pbPutCharArray(const char* stringToPack, char* dst);
void pbPutBytes(const std::vector<uint8_t>& data, pb_bytes_array_t& dst);

const char* pb_encode_to_string(const pb_msgdesc_t* fields, const void* data);
