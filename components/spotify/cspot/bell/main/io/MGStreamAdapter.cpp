// MGStreamAdapter.cpp
#include "MGStreamAdapter.h"

mg_buf::mg_buf(struct mg_connection* _conn) : conn(_conn) {
  setp(buffer, buffer + BUF_SIZE - 1);  // -1 to leave space for overflow '\0'
}

mg_buf::int_type mg_buf::overflow(int_type c) {
  if (c != EOF) {
    *pptr() = c;
    pbump(1);
  }

  if (flush_buffer() == EOF) {
    return EOF;
  }

  return c;
}

int mg_buf::flush_buffer() {
  int len = int(pptr() - pbase());
  if (mg_write(conn, buffer, len) != len) {
    return EOF;
  }
  pbump(-len);  // reset put pointer accordingly
  return len;
}

int mg_buf::sync() {
  if (flush_buffer() == EOF) {
    return -1;  // return -1 on error
  }
  return 0;
}

MGStreamAdapter::MGStreamAdapter(struct mg_connection* _conn)
    : std::ostream(&buf), buf(_conn) {
  rdbuf(&buf);  // set the custom streambuf
}

mg_read_buf::mg_read_buf(struct mg_connection* _conn) : conn(_conn) {
  setg(buffer + BUF_SIZE,   // beginning of putback area
       buffer + BUF_SIZE,   // read position
       buffer + BUF_SIZE);  // end position
}

mg_read_buf::int_type mg_read_buf::underflow() {
  if (gptr() < egptr()) {  // buffer not exhausted
    return traits_type::to_int_type(*gptr());
  }

  char* base = buffer;
  char* start = base;

  if (eback() == base) {  // true when this isn't the first fill
    // Make arrangements for putback characters
    std::memmove(base, egptr() - 2, 2);
    start += 2;
  }

  // Read new characters
  int n = mg_read(conn, start, buffer + BUF_SIZE - start);
  if (n == 0) {
    return traits_type::eof();
  }

  // Set buffer pointers
  setg(base, start, start + n);

  // Return next character
  return traits_type::to_int_type(*gptr());
}

MGInputStreamAdapter::MGInputStreamAdapter(struct mg_connection* _conn)
    : std::istream(&buf), buf(_conn) {
  rdbuf(&buf);  // set the custom streambuf
}
