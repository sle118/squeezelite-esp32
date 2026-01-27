#include <BinaryStream.h>
#include <stdexcept>  // for runtime_error

using namespace bell;

BinaryStream::BinaryStream(std::ostream* ostr) {
  this->ostr = ostr;
  byteOrder = std::endian::native;
}

BinaryStream::BinaryStream(std::istream* istr) {
  this->istr = istr;
  byteOrder = std::endian::native;
}

void BinaryStream::setByteOrder(std::endian byteOrder) {
  this->byteOrder = byteOrder;
  flipBytes = byteOrder != std::endian::native;
}

void BinaryStream::ensureReadable() {
  if (istr == nullptr)
    throw std::runtime_error("No input provided for binary stream");
}

void BinaryStream::ensureWritable() {
  if (ostr == nullptr)
    throw std::runtime_error("No output provided for binary stream");
}

BinaryStream& BinaryStream::operator>>(int16_t& value) {
  ensureReadable();
  istr->read((char*)&value, sizeof(value));

  if (flipBytes)
    value = swap16(value);

  return *this;
}

BinaryStream& BinaryStream::operator>>(uint16_t& value) {
  ensureReadable();
  istr->read((char*)&value, sizeof(value));
  if (flipBytes)
    swap16(value);

  return *this;
}

BinaryStream& BinaryStream::operator>>(int32_t& value) {
  ensureReadable();
  istr->read((char*)&value, sizeof(value));
  if (flipBytes)
    value = swap32(value);

  return *this;
}

BinaryStream& BinaryStream::operator>>(uint32_t& value) {
  ensureReadable();
  istr->read((char*)&value, sizeof(value));
  if (flipBytes)
    value = swap32(value);

  return *this;
}

BinaryStream& BinaryStream::operator>>(int64_t& value) {
  ensureReadable();
  istr->read((char*)&value, sizeof(value));
  if (flipBytes)
    value = swap64(value);

  return *this;
}

BinaryStream& BinaryStream::operator>>(uint64_t& value) {
  ensureReadable();
  istr->read((char*)&value, sizeof(value));
  if (flipBytes)
    value = swap64(value);

  return *this;
}

BinaryStream& BinaryStream::operator<<(char value) {
  ensureWritable();
  ostr->write((const char*)&value, sizeof(value));

  return *this;
}

BinaryStream& BinaryStream::operator<<(std::byte value) {
  ensureWritable();
  ostr->write((const char*)&value, sizeof(value));

  return *this;
}

BinaryStream& BinaryStream::operator<<(int16_t value) {
  ensureWritable();
  if (flipBytes)
    value = swap16(value);
  ostr->write((const char*)&value, sizeof(value));

  return *this;
}

BinaryStream& BinaryStream::operator<<(uint16_t value) {
  ensureWritable();
  if (flipBytes)
    value = swap16(value);
  ostr->write((const char*)&value, sizeof(value));

  return *this;
}

BinaryStream& BinaryStream::operator<<(int32_t value) {
  ensureWritable();
  if (flipBytes)
    value = swap32(value);
  ostr->write((const char*)&value, sizeof(value));

  return *this;
}

BinaryStream& BinaryStream::operator<<(uint32_t value) {
  ensureWritable();
  if (flipBytes)
    value = swap32(value);
  ostr->write((const char*)&value, sizeof(value));

  return *this;
}

BinaryStream& BinaryStream::operator<<(int64_t value) {
  ensureWritable();
  if (flipBytes)
    value = swap64(value);
  ostr->write((const char*)&value, sizeof(value));

  return *this;
}

BinaryStream& BinaryStream::operator<<(uint64_t value) {
  ensureWritable();
  if (flipBytes)
    value = swap64(value);
  ostr->write((const char*)&value, sizeof(value));

  return *this;
}
