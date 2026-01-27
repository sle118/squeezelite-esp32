#ifdef ESP8266
#  include "pgmspace.h"
#elif defined(ESP_PLATFORM) && __has_include(<pgm_space.h>)
#  include <pgm_space.h>
#else
#  define PROGMEM
#  define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#  define pgm_read_word(addr) (*(const unsigned short *)(addr))
#endif