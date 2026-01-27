#ifndef CSPOT_ASSERT_H
#define CSPOT_ASSERT_H
#include <stdio.h>
#include <cassert>

#define CSPOT_ASSERT(CONDITION, MESSAGE)                                      \
  do {                                                                        \
    if (!(CONDITION)) {                                                       \
      printf("At %s in %s:%d\n  Assertion %s failed: %s", __func__, __FILE__, \
             __LINE__, #CONDITION, MESSAGE);                                  \
      abort();                                                                \
    }                                                                         \
  } while (0)

#endif
