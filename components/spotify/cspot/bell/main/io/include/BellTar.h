#pragma once

#include <iostream>  // for istream, ostream
#include <string>    // for string

namespace bell::BellTar {
typedef long long unsigned file_size_t;

////////////////////////////////////////
// Writing raw data
////////////////////////////////////////
class writer {
  std::ostream& _dst;

 public:
  writer(std::ostream& dst) : _dst(dst) {}
  ~writer() { finish(); }

  // Append the data specified by |data| and |file_size| into the
  // tar file represented by |dst|.
  // In the tar file, the data will be available at |path_in_tar|.
  void put(std::string path_in_tar, char const* const data,
           const file_size_t data_size);

  // Write empty folder at |path_in_tar|.
  // NOTE: to specify folders for files, just use / in the path
  //       passed to |put()|.
  void put_directory(std::string path_in_tar);

  // Call after everything has been added to the tar represented
  // by |dst| to make it a valid tar file.
  void finish();
};

////////////////////////////////////////
// Reading raw data
////////////////////////////////////////
class reader {
  std::istream& _inp;
  struct {
    std::string file_name;
    file_size_t file_size;
    char file_type;
  } _cached_header_data;
  bool _cached_header_data_valid;
  void _cache_header();
  int _number_of_files;

 public:
  // Constructor, pass input stream |inp| pointing to a tar file.
  reader(std::istream& inp)
      : _inp(inp), _cached_header_data_valid(false), _number_of_files(-1) {}

  // Returns true if another file can be read from |inp|.
  bool contains_another_file();

  // Returns file name of next file in |inp|.
  std::string get_next_file_name();
  // Returns file size of next file in |inp|. Use to allocate
  // memory for the |read_next_file()| call.
  file_size_t get_next_file_size();

  // Read next file in |inp| to |data|.
  void read_next_file(char* const data);

  char get_next_file_type();

  void extract_all_files(std::string output_dir);
  // Skip next file in |inp|.
  void skip_next_file();

  // Returns number of files in tar at |inp|.
  int number_of_files();
};
}  // namespace bell::BellTar
