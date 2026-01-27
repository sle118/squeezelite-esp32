#pragma once

#include <cstring>
#include <iostream>
#include <ostream>
#include "civetweb.h"

const size_t BUF_SIZE = 1024;

// Custom streambuf
class mg_buf : public std::streambuf {
 private:
  struct mg_connection* conn;
  char buffer[BUF_SIZE];

 public:
  mg_buf(struct mg_connection* _conn);

 protected:
  virtual int_type overflow(int_type c);
  int flush_buffer();
  virtual int sync();
};

/**
 * @brief Adapts ostream to mg_write
 * 
 */
class MGStreamAdapter : public std::ostream {
 private:
  mg_buf buf;

 public:
  MGStreamAdapter(struct mg_connection* _conn);
};

// Custom streambuf
class mg_read_buf : public std::streambuf {
 private:
  struct mg_connection* conn;
  char buffer[BUF_SIZE];

 public:
  mg_read_buf(struct mg_connection* _conn);

 protected:
  virtual int_type underflow();
};

/**
 * @brief Adapts istream to mg_read
 */
class MGInputStreamAdapter : public std::istream {
 private:
  mg_read_buf buf;

 public:
  MGInputStreamAdapter(struct mg_connection* _conn);
};
