#include "BellTar.h"

#include <sys/stat.h>  // for mkdir

using namespace bell::BellTar;

#include <algorithm>  // for min
#include <cassert>    // for assert
#include <cstdint>    // for uint8_t
#include <cstdio>     // for sprintf, size_t, sscanf, EOF, NULL
#include <cstdlib>    // for rand
#include <cstring>    // for memset, strlen
#include <ctime>      // for time
#include <fstream>    // for ofstream
#include <vector>     // for vector
#ifdef _WIN32
#include <direct.h>
#endif

#ifdef ENABLE_LOGGING
#define LOG printf
#else
#ifdef _WIN32
#define LOG(fmt, ...) ((void)0)
#else
#define LOG(fmt, args...) ((void)0)
#endif
#endif

const char FILL_CHAR = '\0';
const int FILE_NAME_LENGTH = 100;

// From http://en.wikipedia.org/wiki/Tar_(computing)#UStar_format
typedef enum tar_file_type {
  tar_file_type_normal = '0',
  tar_file_type_hard_link = '1',
  tar_file_type_soft_link = '2',
  tar_file_type_directory = '5'
} tar_file_type_t;

struct tar_header {
  char name[FILE_NAME_LENGTH];  // file name
  char mode[8];                 // file mode
  char uid[8];                  // Owner's numeric user ID
  char gid[8];                  // Group's numeric user ID
  char size[12];                // File size in bytes (octal base)
  char mtime[12];               // Last modification time in
                                // numeric Unix time format (octal)
  char checksum[8];             // Checksum for header record
  char typeflag[1];             // file type, see tar_file_type_t
  char linkname[100];           // Name of linked file
  char magic[6];                // UStar indicator "ustar"
  char version[2];              // UStar version "00"
  char uname[32];               // Owner user name
  char gname[32];               // Owner group name
  char devmajor[8];             // Device major number
  char devminor[8];             // Device minor number
  char prefix[155];             // Filename prefix
  char pad[12];                 // padding
};

void header_set_metadata(tar_header* header) {
  std::memset(header, 0, sizeof(tar_header));

  std::sprintf(header->magic, "ustar");
  std::sprintf(header->mtime, "%011lo", (unsigned long)std::time(NULL));
  std::sprintf(header->mode, "%07o", 0644);
  std::sprintf(header->uname, "unkown");  // ... a bit random
  std::sprintf(header->gname, "users");
  header->typeflag[0] = 0;  // always just a normal file
}

/* From Wikipedia: The checksum is calculated by taking the sum of the
         * unsigned byte values of the header record with the eight checksum
         * bytes taken to be ascii spaces. */
void header_set_checksum(tar_header* header) {
  unsigned int sum = 0;

  char* pointer = (char*)header;
  char* end = pointer + sizeof(tar_header);

  // Iterate over header struct until we are at checksum field.
  while (pointer < header->checksum) {
    sum += *pointer & 0xff;
    pointer++;
  }

  // ... then add eight 'ascii spaces' ...
  sum += ' ' * 8;
  pointer += 8;

  // ... and go until the end.
  while (pointer < end) {
    sum += *pointer & 0xff;
    pointer++;
  }

  std::sprintf(header->checksum, "%06o", sum);
}

void header_set_filetype(tar_header* header, tar_file_type_t file_type) {
  header->typeflag[0] = file_type;
}

tar_file_type_t header_get_filetype(tar_header* header) {
  return tar_file_type_t(header->typeflag[0]);
}

void header_set_filesize(tar_header* header, file_size_t file_size) {
  std::sprintf(header->size, "%011llo", file_size);
}

file_size_t header_get_filesize(tar_header* header) {
  file_size_t file_size;
  std::sscanf(header->size, "%011llo", &file_size);
  return file_size;
}

void header_set_filename(tar_header* header, const char* file_name) {
  size_t len = std::strlen(file_name);

  // len > 0 also ensures that the header does not start with \0
  if (len == 0 || len >= FILE_NAME_LENGTH) {
    LOG("Invalid file name for tar: %s\n", file_name);
    std::sprintf(header->name, "INVALID_%d", std::rand());
  } else {
    std::sprintf(header->name, "%s", file_name);
  }
}

std::string header_get_filename(tar_header* header) {
  return std::string(header->name);
}

////////////////////////////////////////

/* Every file in a tar file starts with the tar header */
void _write_header(std::ostream& dst, const char* file_name,
                   file_size_t file_size,
                   tar_file_type_t file_type = tar_file_type_normal) {
  tar_header header;
  header_set_metadata(&header);
  header_set_filename(&header, file_name);
  header_set_filesize(&header, file_size);
  header_set_filetype(&header, file_type);
  header_set_checksum(&header);

  dst.write((const char*)&header, sizeof(tar_header));
}

void _read_header(std::istream& inp, tar_header* header) {
  inp.read((char*)header, sizeof(tar_header));
}

/* The length of the data after the header must be rounded up to a
           multiple of 512 bytes, the length of the header. */
void _fill(std::ostream& dst, unsigned long file_size) {
  while (file_size % sizeof(tar_header) != 0) {
    dst.put(FILL_CHAR);
    file_size++;
  }
}
bool _check_if_header_is_next(std::istream& inp) {
  if (inp.eof() || inp.peek() == EOF) {
    LOG("Can not read next file info, istream at EOF.\n");
    return false;
  }

  if (inp.peek() == FILL_CHAR) {
    LOG("Can not read next file info, istream is pointing "
        "to %d, which a tar header can not start with.\n",
        FILL_CHAR);
    return false;
  }

  return true;
}

void _seek_to_next_header(std::istream& inp) {
  // Advance to start of next header or to end of file
  // Works because
  // - header never starts with FILL_CHAR
  // - at end of file, peek() returns EOF.
  // - FILL_CHAR != EOF
  while (inp.peek() == FILL_CHAR)
    inp.get();
}

////////////////////////////////////////
// writer Implementation
////////////////////////////////////////

void writer::put(std::string path_in_tar, char const* const data,
                 const file_size_t data_size) {
  _write_header(_dst, path_in_tar.c_str(), data_size);
  _dst.write(data, data_size);
  _fill(_dst, data_size);
}

void writer::put_directory(std::string path_in_tar) {
  _write_header(_dst, path_in_tar.c_str(), 0, tar_file_type_directory);
}

/* The end of an tar is marked by at least two consecutive zero-filled
         * records, a record having the size of the header. */
void writer::finish() {
  unsigned long i = 0;
  while (i < 2 * sizeof(tar_header)) {
    _dst.put(FILL_CHAR);
    i++;
  }
}

////////////////////////////////////////
// reader Implementation
////////////////////////////////////////

bool reader::contains_another_file() {
  return _check_if_header_is_next(_inp);
}

void reader::_cache_header() {
  if (_cached_header_data_valid)
    return;

  assert(contains_another_file());

  tar_header h;
  _read_header(_inp, &h);

  _cached_header_data.file_name = header_get_filename(&h);
  _cached_header_data.file_size = header_get_filesize(&h);
  _cached_header_data.file_type = h.typeflag[0];
  _cached_header_data_valid = true;
}

std::string reader::get_next_file_name() {
  _cache_header();
  return _cached_header_data.file_name;
}

file_size_t reader::get_next_file_size() {
  _cache_header();
  return _cached_header_data.file_size;
}

void reader::read_next_file(char* const data) {
  _inp.read(data, get_next_file_size());

  _cached_header_data_valid = false;
  _seek_to_next_header(_inp);
}

void reader::skip_next_file() {
  _inp.seekg(get_next_file_size(), std::ios::cur);

  _cached_header_data_valid = false;
  _seek_to_next_header(_inp);
}

char reader::get_next_file_type() {
  _cache_header();
  return _cached_header_data.file_type;
}

int reader::number_of_files() {
  if (_number_of_files == -1) {
    std::streampos current_position = _inp.tellg();
    _inp.seekg(0, std::ios::beg);

    _number_of_files = 0;
    while (contains_another_file()) {
      _number_of_files++;
      skip_next_file();
    }

    _inp.seekg(current_position);
  }

  return _number_of_files;
}

void reader::extract_all_files(std::string dest_directory) {
  std::vector<uint8_t> scratch_buffer(1024);

  while (contains_another_file()) {
    char fileType = get_next_file_type();
    auto fileName = get_next_file_name();

    // 0 is the normal file type, skip apple's ._ files
#if __cplusplus >= 202002L
    if (fileType == '0' && !fileName.starts_with("._")) {
#else
    if (fileType == '0' && fileName.find("._") != 0) {
#endif
      std::string path = dest_directory + "/" + fileName;

      size_t pos = 0;
      while ((pos = path.find('/', pos)) != std::string::npos) {
        std::string dir = path.substr(0, pos);
        // Create the directory if it doesn't exist
#ifdef _WIN32
        mkdir(dir.c_str());
#else
        mkdir(dir.c_str(), 0777);
#endif
        pos++;
      }

      std::ofstream out(path, std::ios::binary);

      size_t read_size = 0;
      size_t file_size = get_next_file_size();

      while (read_size < file_size) {
        size_t to_read = std::min(file_size - read_size, scratch_buffer.size());

        _inp.read((char*)scratch_buffer.data(), to_read);

        // Move the read size forward
        read_size += _inp.gcount();

        // Write the data to the destination file
        out.write((char*)scratch_buffer.data(), _inp.gcount());
      }

      _cached_header_data_valid = false;
      _seek_to_next_header(_inp);
    } else {
      skip_next_file();
    }
  }
}