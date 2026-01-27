#include "FileStream.h"

#include <stdexcept>  // for runtime_error

#include "BellLogger.h"  // for bell

using namespace bell;

FileStream::FileStream(const std::string& path, std::string read) {
  file = fopen(path.c_str(), "rb");
  if (file == NULL) {
    throw std::runtime_error("Could not open file: " + path);
  }
}

FileStream::~FileStream() {
  close();
}

size_t FileStream::read(uint8_t* buf, size_t nbytes) {
  if (file == NULL) {
    throw std::runtime_error("Stream is closed");
  }

  return fread(buf, 1, nbytes, file);
}

size_t FileStream::skip(size_t nbytes) {
  if (file == NULL) {
    throw std::runtime_error("Stream is closed");
  }

  return fseek(file, nbytes, SEEK_CUR);
}

size_t FileStream::position() {
  if (file == NULL) {
    throw std::runtime_error("Stream is closed");
  }

  return ftell(file);
}

size_t FileStream::size() {
  if (file == NULL) {
    throw std::runtime_error("Stream is closed");
  }

  size_t pos = ftell(file);
  fseek(file, 0, SEEK_END);
  size_t size = ftell(file);
  fseek(file, pos, SEEK_SET);
  return size;
}

void FileStream::close() {
  if (file != NULL) {
    fclose(file);
    file = NULL;
  }
}